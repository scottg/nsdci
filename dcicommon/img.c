/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 *
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */

#include "dci.h"

static char rcsid[] = "$Id$";

/*
 * JPEG markers consist of one or more 0xFF bytes, followed by a marker
 * code byte (which is not an FF).  Here are the marker codes of interest
 * in this program.  (See jdmarker.c for a more complete list.)
 */

#define M_SOF0  0xC0	/* Start Of Frame N */
#define M_SOF1  0xC1	/* N indicates which compression process */
#define M_SOF2  0xC2	/* Only SOF0 and SOF1 are now in common use */
#define M_SOF3  0xC3
#define M_SOF4  0xC4
#define M_SOF5  0xC5
#define M_SOF6  0xC6
#define M_SOF7  0xC7
#define M_SOF9  0xC9
#define M_SOF10 0xCA
#define M_SOF11 0xCB
#define M_SOF13 0xCD
#define M_SOF14 0xCE
#define M_SOF15 0xCF
#define M_SOI   0xD8	/* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9	/* End Of Image (end of datastream) */
#define M_SOS   0xDA	/* Start Of Scan (begins compressed data) */
#define M_COM   0xFE	/* COMment */
#define M_APP12	0xEC	/* (we don't bother to list all 16 APPn's) */

/*
 * The following struct defines a cache key based on file device and inode.
 * This enables the same file with multiple symbolic links to be cached once.
 */

typedef struct Key {
    dev_t dev;
    ino_t ino;
} Key;

/*
 * The following struct holds the cached stat, type, and dimensions of a
 * valid image file.
 */

typedef struct Stat {
    time_t mtime;
    size_t size;
    const char *type;
    char width[16];
    char height[16];
} Stat;

/*
 * Private functions defined in this file.
 */

static Tcl_ObjCmdProc ImgObjCmd;
static int ImgStat(Tcl_Interp *interp, Tcl_Obj *filePtr, Stat *stPtr);
static const char *GetImgType(Tcl_Channel chan);
static int GifSize(Tcl_Channel chan, int *widthPtr, int *heightPtr); 
static int BmpSize(Tcl_Channel chan, int *widthPtr, int *heightPtr); 
static int JpegSize(Tcl_Channel chan, int *widthPtr, int *heightPtr); 
static int read_1_byte(Tcl_Channel chan);
static int read_2_bytes(Tcl_Channel chan, unsigned int *retb);
static int skip_variable(Tcl_Channel chan);
static int next_marker(Tcl_Channel chan);

/*
 * Static variables defined in this file.
 */

static Ns_Cache *cache;
static int cachesize;

/*
 * The following constant strings for known types compared directly
 * in the code below.
 */

static const char *jpeg = "jpeg";
static const char *gif = "gif";
static const char *bmp = "bmp";
static const char *art = "art";


void
DciImgLibInit(void)
{
    DciAddIdent(rcsid);
    /* NB: Default cache size can be changed via module config. */
    cachesize = 1000;
}


int
DciImgTclInit(Tcl_Interp *interp)
{
    /*
     * Create the cache if necessary.
     */

    if (cache == NULL) {
	Ns_MasterLock();
	if (cache == NULL) {
    	    cache = Ns_CacheCreateSz("dci:stimg", sizeof(Key)/sizeof(int),
				     cachesize, ns_free);
	}
	Ns_MasterUnlock();
    }
    Tcl_CreateObjCommand(interp, "dci.jpegsize", ImgObjCmd, (ClientData) 'j',
			 NULL);
    Tcl_CreateObjCommand(interp, "dci.gifsize",  ImgObjCmd, (ClientData) 'g',
			 NULL);
    Tcl_CreateObjCommand(interp, "dci.bmpsize",  ImgObjCmd, (ClientData) 'b',
			 NULL);
    Tcl_CreateObjCommand(interp, "dci.imgtype",  ImgObjCmd, (ClientData) 't',
			 NULL);
    Tcl_CreateObjCommand(interp, "dci.imgsize",  ImgObjCmd, (ClientData) 'd',
			 NULL);
    Tcl_CreateObjCommand(interp, "dci.imgstat",  ImgObjCmd, (ClientData) 's',
			 NULL);
    return TCL_OK;
}


int
DciImgModInit(char *server, char *module)
{
    char *path;
    int n;

    path = Ns_ConfigGetPath(server, module, NULL);
    if (Ns_ConfigGetInt(path, "imgcachesize", &n) && n > cachesize) {
	cachesize = n;
    }
    return NS_OK;
}


static int
ImgObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    const char *type;
    int cmd = (int) arg;
    Stat st;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "file");
	return TCL_ERROR;
    }
    if (!ImgStat(interp, objv[1], &st)) {
	return TCL_ERROR;
    }

    /*
     * Ensure file is of proper type for dci.jpegsize, dci.gifsize, and
     * dci.bmpsize.
     */

    switch (cmd) {
    case 'j':
	type = jpeg;
	break;
    case 'g':
	type = gif;
	break;
    case 'b':
	type = bmp;
	break;
    default:
	goto append;
	break;
    }
    if (type != st.type) {
	Tcl_AppendResult(interp, "not a ", type, " file: ",
			 Tcl_GetString(objv[1]), NULL);
	return TCL_ERROR;
    }
    cmd = 'd';

append:

    /*
     * Append the type and/or dimensions.
     */

    if (cmd == 's' || cmd == 't') {
	Tcl_AppendElement(interp, (char *) st.type);
    }
    if (cmd == 's' || cmd == 'd') {
	Tcl_AppendElement(interp, st.width);
	Tcl_AppendElement(interp, st.height);
    }
    return TCL_OK;
}


static int 
ImgStat(Tcl_Interp *interp, Tcl_Obj *filePtr, Stat *bufPtr)
{
    Ns_Entry *entry;
    Stat *stPtr;
    Key key;
    struct stat st;
    Tcl_Channel chan;
    const char *type;
    int new, err, w, h;

    err = Tcl_FSStat(filePtr, &st);
    if (err == 0 && S_ISDIR(st.st_mode)) {
	err = errno = EISDIR;
    }
    if (err != 0) {
	Tcl_AppendResult(interp, "could not stat \"", Tcl_GetString(filePtr),
	    "\": ", Tcl_PosixError(interp), NULL);
	return 0;
    }
    key.dev = st.st_dev;
    key.ino = st.st_ino;
    Ns_CacheLock(cache);
    stPtr = NULL;
    entry = Ns_CacheCreateEntry(cache, (char *) &key, &new);
    if (!new) {
	while (entry != NULL && (stPtr = Ns_CacheGetValue(entry)) == NULL) {
	    entry = Ns_CacheFindEntry(cache, (char *) &key);
	}
	if (stPtr != NULL &&
		(stPtr->mtime != st.st_mtime || stPtr->size != st.st_size)) {
	    Ns_CacheUnsetValue(entry);
	    stPtr = NULL;
	}
    }
    if (stPtr == NULL) {
	Ns_CacheUnlock(cache);
    	chan = Tcl_FSOpenFileChannel(interp, filePtr, "r", 0);
	if (chan != NULL) {
	    type = GetImgType(chan);
	    if (type == NULL) {
		Tcl_AppendResult(interp, "invalid imge file: ",
				 Tcl_GetString(filePtr), NULL);
	    } else {
		if (type == jpeg) {
		    JpegSize(chan, &w, &h);
	    	} else if (type == gif) {
		    GifSize(chan, &w, &h);
	    	} else if (type == bmp) {
		    BmpSize(chan, &w, &h);
		} else {
		    w = h = 0;
		}
	    	stPtr = ns_malloc(sizeof(Stat));
		stPtr->type = type;
	    	stPtr->mtime = st.st_mtime;
	    	stPtr->size = st.st_size;
		sprintf(stPtr->width, "%d", w);
		sprintf(stPtr->height, "%d", h);
	    }
	    Tcl_Close(interp, chan);
	}
	Ns_CacheLock(cache);
	entry = Ns_CacheCreateEntry(cache, (char *) &key, &new);
	if (stPtr == NULL) {
	    Ns_CacheFlushEntry(entry);
	} else { 
	    Ns_CacheSetValueSz(entry, stPtr, 1);
	}
	Ns_CacheBroadcast(cache);
    }
    if (stPtr != NULL) {
	*bufPtr = *stPtr;
    }
    Ns_CacheUnlock(cache);
    if (stPtr == NULL) {
	return 0;
    }
    return 1;
}


static const char *
GetImgType(Tcl_Channel chan)
{
    unsigned char buf[3];
    const char *type;

    /*
     * Type of known images formats can be determined in the first 3 bytes.
     */

    type = NULL;
    if (Tcl_Read(chan, buf, 3) == 3) {
    	if (buf[0] == 0xff && buf[1] == M_SOI) {
            type = jpeg;
    	} else if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') {
            type = gif;
    	} else if (buf[0] == 'J' && buf[1] == 'G') {
            type = art;
    	} else if (buf[0] == 'B' && buf[1] == 'M') {
            type = bmp;
    	}
    }
    return type;
}


static int
GifSize(Tcl_Channel chan, int *widthPtr, int *heightPtr)
{
    unsigned char buf[4];

    /*
     * GIF size is 2 2-byte shorts 6 bytes into the file.
     */

    if (Tcl_Seek(chan, 6, SEEK_SET) < 0 || Tcl_Read(chan, buf, 4) != 4) {
	return 0;
    }
    *widthPtr = (int) ((buf[1] << 8) | buf[0]);
    *heightPtr = (int) ((buf[3] << 8) | buf[2]);
    return 1;
}


static int
BmpSize(Tcl_Channel chan, int *widthPtr, int *heightPtr)
{
    unsigned char  buf[8];

    /*
     * BMP size is 2 4-byte ints 18 bytes into the file.
     */

    if (Tcl_Seek(chan, 18, SEEK_SET) < 0 || Tcl_Read(chan, buf, 8) != 8) {
	return 0;
    }
    *widthPtr = ((buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0]);
    *heightPtr = ((buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4]);
    return 1;
}


static int
JpegSize(Tcl_Channel chan, int *widthPtr, int *heightPtr)
{
    unsigned int w, h;
    unsigned int dummy;
    int          marker;

    /*
     * Jpeg size is buried somewhere beyond several markers.
     */

    while (1) {
	marker = next_marker(chan);
	switch (marker) {
        /* Note that marker codes 0xC4, 0xC8, 0xCC are not, and must not be,
         * treated as SOFn.  C4 in particular is actually DHT.
         */
	case M_SOF0:	/* Baseline */
	case M_SOF1:	/* Extended sequential, Huffman */
	case M_SOF2:	/* Progressive, Huffman */
	case M_SOF3:	/* Lossless, Huffman */
	case M_SOF5:	/* Differential sequential, Huffman */
	case M_SOF6:	/* Differential progressive, Huffman */
	case M_SOF7:	/* Differential lossless, Huffman */
	case M_SOF9:	/* Extended sequential, arithmetic */
	case M_SOF10:	/* Progressive, arithmetic */
	case M_SOF11:	/* Lossless, arithmetic */
	case M_SOF13:	/* Differential sequential, arithmetic */
	case M_SOF14:	/* Differential progressive, arithmetic */
	case M_SOF15:	/* Differential lossless, arithmetic */
	    if (read_2_bytes(chan, &dummy) != EOF
		&& read_1_byte(chan) != EOF
		&& (read_2_bytes(chan, &h) != EOF)
                && (read_2_bytes(chan, &w) != EOF)) {
		*widthPtr = (int) w;
		*heightPtr = (int) h;
		return 1;
            }
	    goto err;
	    break;
	case EOF:
	case M_SOS:
	case M_EOI:
	    goto err;
	    break;
	case M_COM:
	case M_APP12:
	default:
	    if (!skip_variable(chan)) {
	    	goto err;
	    }
            break;
        }
    }
err:
    return 0;
}

/*
 * Read a single byte.
 */

static int
read_1_byte(Tcl_Channel chan)
{
    unsigned char buf[1];

    if (Tcl_Read(chan, (char *) buf, 1) != 1) {
	return EOF;
    }
    return (int) buf[0];
}

/*
 * Read 2 bytes, convert to unsigned int.
 * All 2-byte quantities in JPEG markers are MSB first.
 */

static int
read_2_bytes(Tcl_Channel chan, unsigned int *retb)
{
  int c1, c2;

  c1 = read_1_byte(chan);
  c2 = read_1_byte(chan);
  if (c1 == EOF || c2 == EOF)
    return EOF;
  *retb = (((unsigned int) c1) << 8) + ((unsigned int) c2);
  return 1;
}

/*
 * Find the next JPEG marker and return its marker code.
 * We expect at least one FF byte, possibly more if the compressor used FFs
 * to pad the file.
 * There could also be non-FF garbage between markers.  The treatment of such
 * garbage is unspecified; we choose to skip over it but emit a warning msg.
 * NB: this routine must not be used after seeing SOS marker, since it will
 * not deal correctly with FF/00 sequences in the compressed image data...
 */

static int
next_marker(Tcl_Channel chan)
{
    int c;

    /* Find 0xFF byte; count and skip any non-FFs. */
    do {
    	c = read_1_byte(chan);
    } while (c != EOF && c != 0xFF);
    if (c != EOF) {
	/* Get marker code byte, swallowing any duplicate FF bytes.  */
	do {
	    c = read_1_byte(chan);
	} while (c == 0xFF);
    }
    return c;
}

/*
 * Skip over an unknown or uninteresting variable-length marker
 */

static int
skip_variable(Tcl_Channel chan)
{
    unsigned int length;

    /* Get the marker parameter length count */
    if (read_2_bytes(chan, &length) == EOF) {
	return 0;
    }
    /* Length includes itself, so must be at least 2 */
    length -= 2;
    /* Skip over the remaining bytes */
    while (length > 0) {
        if (read_1_byte(chan) == EOF) {
	    return 0;
	}
        length--;
    }
    return 1;
}
