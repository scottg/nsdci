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

#define BUFSZ 1024
#define EOF (-1)

typedef struct Base64 {
    char    *buf;
    int      size;
    int	     next;
    Tcl_DString out;
} Base64;

static unsigned char base64 [] = {
    /*       0   1   2   3   4   5   6   7   */
    'A','B','C','D','E','F','G','H', /* 0 */
    'I','J','K','L','M','N','O','P', /* 1 */
    'Q','R','S','T','U','V','W','X', /* 2 */
    'Y','Z','a','b','c','d','e','f', /* 3 */
    'g','h','i','j','k','l','m','n', /* 4 */
    'o','p','q','r','s','t','u','v', /* 5 */
    'w','x','y','z','0','1','2','3', /* 6 */
    '4','5','6','7','8','9','+','/'  /* 7 */
};


static int Decode(Base64 *);
static int Encode(Base64 *);
static int NextChar(Base64 *);
static void Append(int, Base64 *);
static Tcl_CmdProc Base64Cmd;


void
DciBase64LibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciBase64TclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "base64.encode", Base64Cmd, (ClientData) 'e',
	 	      NULL);
    Tcl_CreateCommand(interp, "base64.decode", Base64Cmd, (ClientData) 'd',
		      NULL);
    return TCL_OK;
}


static int
Base64Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Base64 *bs, bsbuf;
    struct stat st;
    int result, cmd, ok, len, fd;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " infile outfile\"", NULL);
	    return TCL_ERROR;
    }
    cmd = (int) arg;
    result = TCL_ERROR;
    bs = &bsbuf;
    bs->next = 0;
    bs->buf = NULL;
    Tcl_DStringInit(&bs->out);
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    argv[1], "\"", Tcl_PosixError(interp), NULL);
	goto done;
    }
    if (fstat(fd, &st) != 0) {
	Tcl_AppendResult(interp, "could not fstat \"",
	    argv[1], "\"", Tcl_PosixError(interp), NULL);
	goto done;
    }
    bs->size = st.st_size;
    bs->buf = ns_malloc((size_t)bs->size);
    len = read(fd, bs->buf, (size_t)bs->size);
    if (len != bs->size) {
	Tcl_AppendResult(interp, "could not read \"",
	    argv[1], "\"", Tcl_PosixError(interp), NULL);
	goto done;
    }
    close(fd);
    fd = -1;
    if (cmd == 'e') {
	len = (bs->size / 3 + 1) * 4;
	len += (len / 72) * 2;
	Tcl_DStringSetLength(&bs->out, len);
	ok = Encode(bs);
    } else {
	len = bs->size / 4 * 3;
	len -= (len / 72) * 2;
	Tcl_DStringSetLength(&bs->out, len);
	ok = Decode(bs);
    }
    if (!ok) {
	Tcl_AppendResult(interp, "invalid input: ", argv[1], NULL);
    } else {
	fd = open(argv[2], O_CREAT|O_WRONLY|O_EXCL, 0644);
	if (fd < 0) {
	    Tcl_AppendResult(interp, "could not open \"", argv[2],
		"\": ", Tcl_PosixError(interp), NULL);
	} else {
	    len = bs->out.length;
	    if (write(fd, bs->out.string, (size_t)len) == len) {
		result = TCL_OK;
	    } else {
		Tcl_AppendResult(interp, "could not write \"", argv[2],
		    "\": ", Tcl_PosixError(interp), NULL);
	    }
	}
    }
done:
    if (fd >= 0) {
	close(fd);
    }
    if (bs->buf != NULL) {
	ns_free(bs->buf);
    }
    Tcl_DStringFree(&bs->out);
    return result;
}


static int
Decode(Base64 *bs) 
{
    int c;
    int     b1, nn;
    long    b4;
    
    b1 = nn = b4 = 0;
    while ((c = NextChar(bs)) != EOF) {
        if (c >= 'A' && c <= 'Z') {
            b1 = (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            b1 = 26 + (c - 'a');
        } else if (c >= '0' && c <= '9') {
            b1 = 52 + (c - '0');
        } else if (c == '+') {
            b1 = 62;
        } else if (c == '/') {
            b1 = 63;
        } else if (c == '\r' || c == '\n') {
            continue;
        } else {
            if (c == '=') {
	        if (nn == 3) {
	            b4 = (b4 << 6);
		    Append(b4 >> 16, bs);
		    Append(b4 >>  8, bs);
	        } else if (nn == 2) {
	            b4 = (b4 << 12);
		    Append(b4 >> 16, bs);
	        }
            } else {
		/* NB: invalid input. */
		bs->size = -1;
		return 0;
            }
            break;
        }

        b4 = (b4 << 6) | (long)b1;
        nn++;

        if (nn == 4) {
	    Append(b4 >> 16, bs);
	    Append(b4 >>  8, bs);
	    Append(b4, bs);
            nn = 0;
        }
    }
    return 1;
}


static int
Encode(Base64 *bs)
{
    int i, n;
    unsigned char a, b, c;

    i = n = 0;
    while (i < bs->size) {
        a = NextChar(bs);
        b = NextChar(bs);
        c = NextChar(bs);
        if (i + 2 < bs->size) {
	    Append((base64[(a >> 2) & 0x3F]), bs);
	    Append((base64[((a << 4) & 0x30)+((b >> 4) & 0xf)]), bs);
	    Append((base64[((b << 2) & 0x3c)+((c >> 6) & 0x3)]), bs);
	    Append((base64[c & 0x3F]), bs);
        } else if (i + 1 < bs->size) {
	    Append((base64[(a >> 2) & 0x3F]), bs);
	    Append((base64[((a << 4) & 0x30)+((b >> 4) & 0xf)]), bs);
	    Append((base64[((b << 2) & 0x3c)+((c >> 6) & 0x3)]), bs);
	    Append('=', bs);
        } else {
	    Append((base64[(a >> 2) & 0x3F]), bs);
	    Append((base64[((a << 4) & 0x30)+((b >> 4) & 0xf)]), bs);
	    Append('=', bs);
	    Append('=', bs);
        }
        i+=3;
        n += 4;
        if (n == 72) {
	    Tcl_DStringAppend(&bs->out, "\r\n", 2);
            n = 0;
        }
    }
    return 1;
}


int
NextChar(Base64 *bs)
{
    if (bs->next == bs->size) {
        return EOF;
    }
    return (int) (bs->buf[bs->next++]);
}


void
Append(int i, Base64 *bs)
{
    unsigned char c;
    
    c = (unsigned char) i;
    Tcl_DStringAppend(&bs->out, &c, 1);
}
