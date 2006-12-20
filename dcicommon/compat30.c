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

void
DciCompat30LibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciGetOpenFd(Tcl_Interp *interp, char *fid, int write, int *fdPtr)
{
    return Ns_TclGetOpenFd(interp, fid, write, fdPtr);
}


static int
GetWriteChan(Tcl_Interp *interp, char *chanId, Tcl_Channel *chanPtr)
{
    Tcl_Channel chan;
    int mode;
    
    chan = Tcl_GetChannel(interp, chanId, &mode);
    if (chan == NULL) {
    	return TCL_ERROR;
    }
    if (!(mode & TCL_WRITABLE)) {
        Tcl_AppendResult(interp, "channel \"", chanId,
	    "\" not open for write", NULL);
    	return TCL_ERROR;
    }
    *chanPtr = chan;
    return TCL_OK;
}


int
DciWriteChan(Tcl_Interp *interp, char *chanId, char *buf, int len)
{
    Tcl_Channel chan;
    int n;
    
    if (GetWriteChan(interp, chanId, &chan) != TCL_OK) {
    	return TCL_ERROR;
    }
    while (len > 0) {
    	n = Tcl_Write(chan, buf, len);
	if (n < 0) {
	    Tcl_AppendResult(interp, "write failed: ", Tcl_PosixError(interp), NULL);
    	    return TCL_ERROR;
	}
	len -= n;
	buf += n;
    }
    return TCL_OK;
}


int
DciFlush(Tcl_Interp *interp, char *chanId)
{
    Tcl_Channel chan;

    if (GetWriteChan(interp, chanId, &chan) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (Tcl_Flush(chan) != TCL_OK) {
        Tcl_AppendResult(interp, "error flushing \"", Tcl_GetChannelName(chan),
		"\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}
