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
#define	atoi(s)	strtol(s, 0, 0)

typedef struct Ascii85 {
    unsigned long  width;
    unsigned long  pos;
    int            inFD;
    int            outFD;
} Ascii85;

static unsigned long pow85[] = {
	85*85*85*85, 85*85*85, 85*85, 85, 1
};

static int Decode(Ascii85 *);
static int Encode(Ascii85 *);
static int EncodeChar(Ascii85 *, unsigned long, int);
static int Append(unsigned long, Ascii85 *);
static int Wput(unsigned long, int, Ascii85 *);
static Tcl_CmdProc Ascii85Cmd;

void
DciAscii85LibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciAscii85TclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "ascii85.encode", Ascii85Cmd, (ClientData) 'e',
		      NULL);
    Tcl_CreateCommand(interp, "ascii85.decode", Ascii85Cmd, (ClientData) 'd',
		      NULL);
    return TCL_OK;
}


static int
Ascii85Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ascii85 *bs, bsbuf;
    int result, cmd, ok;

    if (argc < 3 || argc > 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " infile outfile {width 72}\"", NULL);
	    return TCL_ERROR;
    }
    cmd = (int) arg;
    result = TCL_ERROR;
    bs = &bsbuf;
    bs->inFD = 0;
    bs->outFD = 0;
    bs->width = 72;

    bs->inFD = open(argv[1], O_RDONLY);
    if (bs->inFD < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    argv[1], "\"", Tcl_PosixError(interp), NULL);
	goto done;
    }

    bs->outFD = open(argv[2],O_CREAT|O_WRONLY, 0644);
    if (bs->outFD < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    argv[2], "\"", Tcl_PosixError(interp), NULL);
	goto done;
    } else {
       if (cmd == 'e') {
           if (argc == 4) {
               bs->width = atoi(argv[3]);
               if (bs->width < 10 || bs->width > 200) {
                   bs->width = 72;
               }
           }
           ok = Encode(bs);
        } else {
           ok = Decode(bs);
        }
    }
    if (!ok) {
	Tcl_AppendResult(interp, "invalid input: ", argv[1], NULL);
    } else {
        result = TCL_OK;
    }
done:
    if (bs->outFD > 0) {
        close (bs->outFD);
    }
    if (bs->inFD > 0) {
        close (bs->inFD);
    }
    return result;
}


static int
Decode(Ascii85 *bs) 
{
    int i, count = 0;
    unsigned long tuple = 0;
    char c;

    while ((read(bs->inFD, (void *) &c, (size_t) 1)) == 1) {
        if (c >= '!' && c <= 'u') {
            tuple += (c - '!') * pow85[count++];
            if (count == 5) {
                Wput(tuple, 4, bs);
                count = 0;
                tuple = 0;
            }
        }  else if (c == 'z') {
            if (count != 0) {
                /* NB: invalid input. */
                return 0;
            }
            for (i = 0; i < 4; i++) {
                if (Append(0, bs) < 1) {
                    return 0;
                } 
            }
        }  else if (c == '~') {
            read(bs->inFD,(void *) &c, (size_t) 1);
            while (c == '\r' || c == '\n' || c == '\t' ||
                   c == '\0' || c == '\f' || c == '\b' || c == 0177) {
                read(bs->inFD,(void *) &c, (size_t) 1);
            }
            if (c == '>') {
                if (count > 0) {
                    count--;
                    tuple += pow85[count];
                    Wput(tuple, count, bs);
                }
                read(bs->inFD, (void *) &c, (size_t) 1);
                return 1;
            } else {
                /* NB: invalid input. */
                return 0;
            }
        } else if (c == '\r' || c == '\n' || c == '\t' || 
                   c == '\0' || c == '\f' || c == '\b' || c == 0177) {
            continue;
        }
    }
    return 1;
}

static int
Encode(Ascii85 *bs)
{

    unsigned char c;
    int count = 0;
    unsigned long tuple = 0;

    bs->pos = 0;

    while ((read(bs->inFD, (void *) &c, (size_t) 1)) == 1) {
        switch (count++) {
        case 0: 
            tuple |= (c << 24); 
            break;
        case 1: 
            tuple |= (c << 16); 
            break;
        case 2:
            tuple |= (c <<  8);
            break;
        case 3:
            tuple |= c;
            if (tuple == 0) {
                if (Append((unsigned long) 'z', bs) < 1) {
                    return 0;
                }
                if (bs->pos++ >= bs->width) {
                    bs->pos = 0;
                    if (Append((unsigned long) '\n', bs) < 1) {
                        return 0;
                    }
                }
            } else {
                if (EncodeChar(bs, tuple, count) < 1) {
                    return 0;
                }
            }

            tuple = 0;
            count = 0;
            break;
        }
    }

    if (count > 0) {
        if (EncodeChar(bs, tuple, count)  < 1) {
            return 0;
        }
    }

    if (bs->pos + 2 > bs->width) {
         if (Append((unsigned long) '\n', bs) < 1) {
             return 0;
         }
    }

    if (Append((unsigned long) '~', bs) < 1) {
        return 0;
    }
    if (Append((unsigned long) '>', bs) < 1) {
        return 0;
    }
    if (Append((unsigned long) '\n', bs) < 1) {
        return 0;
    }
    return 1;
}

static int EncodeChar(Ascii85 *bs, unsigned long tuple, int count) {
    int i;
    char buf[5], *s = buf;
    i = 5;
    do {
        *s++ = tuple % 85;
        tuple /= 85;
    } while (--i > 0);
    i = count;
    do {
        if (Append((unsigned long) *--s + '!', bs) < 1) {
            return 0;
        }
        if (bs->pos++ >= bs->width) {
            bs->pos = 0;
            if (Append((unsigned long) '\n', bs) < 1) {
                return 0;
            }
        }
    } while (i-- > 0);
    return 1;
}

static int Append(unsigned long i, Ascii85 *bs)
{
    unsigned char c;

    c = (unsigned char) i;
    return (write(bs->outFD, (void *) &c, sizeof(c)));
}

static int Wput(unsigned long tuple, int bytes, Ascii85 *bs) {
    switch (bytes) {
    case 4:
        if (Append(tuple >> 24,bs) < 1) {
            return 0;
        }
        if (Append(tuple >> 16,bs)  < 1) {
            return 0;
        }
        if (Append(tuple >>  8,bs) < 1) {
            return 0;
        }
        if (Append(tuple,bs) < 1) {
            return 0;
        }
        break;
    case 3:
        if (Append(tuple >> 24,bs) < 1) {
            return 0;
        }
        if (Append(tuple >> 16,bs) < 1) {
            return 0;
        }
        if (Append(tuple >>  8,bs) < 1) {
            return 0;
        }
        break;
    case 2:
        if (Append(tuple >> 24,bs) < 1) {
            return 0;
        }
        if (Append(tuple >> 16,bs) < 1) {
            return 0;
        }
        break;
    case 1:
        if (Append(tuple >> 24,bs) < 1) {
            return 0;
        }
        break;
    }

    return 1;
}
