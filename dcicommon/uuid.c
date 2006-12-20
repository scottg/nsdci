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

static Tcl_CmdProc GetUuidCmd;

static Ns_Mutex lock;
static unsigned int pid;
static unsigned int addr;


void
DciUuidLibInit(void)
{
    DciAddIdent(rcsid);
    addr = (unsigned int) inet_addr(Ns_InfoAddress());
    pid = (unsigned int) getpid();
}


int
DciUuidTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.uuid_create", GetUuidCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.getuuid", GetUuidCmd, NULL, NULL);
    return TCL_OK;
}


static int
GetUuidCmd(ClientData data,Tcl_Interp *interp, int argc, char **argv)
{
    char buf[64];

    (void) Dci_GetUuid(buf);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


int
Dci_GetUuid(char *buf)
{
    static volatile Ns_Time last;
    Ns_Time now;

    Ns_MutexLock(&lock);
    while (1) {
	Ns_GetTime(&now);
	now.usec /= 1000;
	if (now.sec != last.sec || now.usec != last.usec) {
	    break;
	}
	Ns_ThreadYield();
    }
    last = now;
    Ns_MutexUnlock(&lock);
    sprintf(buf, "%08x-%05x-%05x-%08x",
	(unsigned int) now.sec, (unsigned int) now.usec, pid, addr);
    return 1;
}
