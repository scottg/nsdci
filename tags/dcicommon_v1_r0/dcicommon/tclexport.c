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

static char rcsid[] = "$Id$";

#include "dci.h"

#define DCITCLEXPORT_NUM_FIELDS 3
typedef struct Hdr {
    uint32_t	code;
    uint32_t    len[DCITCLEXPORT_NUM_FIELDS];
} Hdr;

static char *default_x_fmt = DCI_EXPORTFMT_CIR;
static void Init(Ns_DString *dsPtr, char **bufPtr, int *lenPtr);
static void Next(Ns_DString *dsPtr, char **bufPtr, int *lenPtr, int net);


void
DciTclExportLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciTclRead(int fd, Ns_DString *dsPtr, int net)
{
    char *buf;
    int len, n;

    Init(dsPtr, &buf, &len);
    while (len > 0) {
	n = read(fd, buf, (size_t)len);
	if (n <= 0) {
	    return 0;
	}
	buf += n;
	len -= n;
    }
    Next(dsPtr, &buf, &len, net);
    while (len > 0) {
	n = read(fd, buf, (size_t)len);
	if (n <= 0) {
	    return 0;
	}
	buf += n;
	len -= n;
    }
    return 1;
}


int
DciTclRecv(int sock, Ns_DString *dsPtr, int timeout)
{
    char *buf;
    int len, n;

    Init(dsPtr, &buf, &len);
    while (len > 0) {
	n = Ns_SockRecv(sock, buf, len, timeout);
	if (n <= 0) {
	    return 0;
	}
	buf += n;
	len -= n;
    }
    Next(dsPtr, &buf, &len, 1);
    while (len > 0) {
	n = Ns_SockRecv(sock, buf, len, timeout);
	if (n <= 0) {
	    return 0;
	}
	buf += n;
	len -= n;
    }
    return 1;
}


void
DciTclExport(Tcl_Interp *interp, int code, Ns_DString *dsPtr, char *x_fmt)
{
    Hdr hdr;
    char *einfo, *ecode;
    char *res_ref;
    char *strs[DCITCLEXPORT_NUM_FIELDS];
    int  str_lens[DCITCLEXPORT_NUM_FIELDS];
    int   i;

    if (code == TCL_OK) {
	einfo = ecode = NULL;
    } else {
	ecode = Tcl_GetVar(interp, "errorCode", TCL_GLOBAL_ONLY);
	einfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    }
    res_ref = Tcl_GetStringResult(interp);

    /*
     * The message is to be laid out with the elements in the order
     * specified by x_fmt.  Need to first scan through the set of
     * elements to assemble the header with the element lengths in
     * the proper order.  Then, lay the header followed by the
     * elements in the determined order.
     */
    if (x_fmt == NULL) {
        x_fmt = default_x_fmt;
    }

    hdr.code = htonl(code);
    for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
        switch (x_fmt[i]) {
            case 'R':
                strs[i] = res_ref;
                break;
            case 'C':
                strs[i] = ecode;
                break;
            case 'I':
                strs[i] = einfo;
                break;
        }
        str_lens[i] = strs[i] ? strlen(strs[i]) : 0;
        hdr.len[i] = htonl(str_lens[i]);
    }

    Ns_DStringNAppend(dsPtr, (char *) &hdr, sizeof(hdr));
    for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
        if (str_lens[i] > 0) {
            Ns_DStringNAppend(dsPtr, strs[i], str_lens[i]);
        }
    }
}


int
DciTclImport(Tcl_Interp *interp, Ns_DString *dsPtr, char *x_fmt)
{
    Hdr *hdrPtr;
    int code;
    char *str;
    int offset;
    int i;
    int len;

    if (dsPtr->length < sizeof(Hdr)) {
	Tcl_AppendResult(interp, "invalid remote tcl result", TCL_STATIC);
	return TCL_ERROR;
    }

    /*
     * The result, error and info data are stored in the message in
     * the order specified by x_fmt.  The lengths are in the header
     * in the same order.  The strings are unrolled from the message
     * in reverse order, chopping off the (alread consumed) end as
     * we go.  First scan through the lengths to find the end, then
     * do the backward scan to grab the strings.
     */
    if (x_fmt == NULL) {
        x_fmt = default_x_fmt;
    }

    hdrPtr = (Hdr *) dsPtr->string;
    code = ntohl(hdrPtr->code);
    str = (char *) (hdrPtr + 1);
    offset = 0;
    for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
        offset += ntohl(hdrPtr->len[i]);
    }
    for (i = DCITCLEXPORT_NUM_FIELDS - 1; i >= 0; i-- ) {
        len = ntohl(hdrPtr->len[i]);
        offset -= len;
        switch (x_fmt[i]) {
            case 'R':
                Tcl_SetResult(interp, str + offset, TCL_VOLATILE);
                str[offset] = '\0';
                break;
            case 'C':
                if (len > 0) {
                    Tcl_SetErrorCode(interp, str + offset, NULL);
                    str[offset] = '\0';
                }
                break;
            case 'I':
                if (len > 0) {
                    Tcl_AddErrorInfo(interp, str + offset);
                    str[offset] = '\0';
                }
                break;
        }
    }
    return code;
}


static void
Init(Ns_DString *dsPtr, char **bufPtr, int *lenPtr)
{
    Hdr *hdrPtr;
    int len;

    len = sizeof(Hdr);
    Ns_DStringSetLength(dsPtr, len);
    hdrPtr = (Hdr *) dsPtr->string;
    memset(hdrPtr, 0, (size_t)len);
    *bufPtr = (char *) hdrPtr;
    *lenPtr = len;
}


static void
Next(Ns_DString *dsPtr, char **bufPtr, int *lenPtr, int net)
{
    Hdr *hdrPtr;
    int len;
    int i;

    hdrPtr = (Hdr *) dsPtr->string;
    if (net) {
        len = 0;
        for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
            len += ntohl(hdrPtr->len[i]);
        }
    } else {
	len = 0;
        for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
            len += hdrPtr->len[i];
        }
	hdrPtr->code = htonl(hdrPtr->code);
        for (i = 0; i < DCITCLEXPORT_NUM_FIELDS; i++ ) {
            hdrPtr->len[i] = htonl(hdrPtr->len[i]);
        }
    }
    Ns_DStringSetLength(dsPtr, (int)(sizeof(Hdr) + len));
    *bufPtr = dsPtr->string + sizeof(Hdr);
    *lenPtr = len;
}
