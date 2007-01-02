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

typedef struct {
    Tcl_CmdProc *evalProc;
    ClientData   evalData;
    Tcl_CmdProc *putsProc;
    ClientData   putsData;
} Procs;

static char *key = "dci:eval";
static Procs *GetProcs(Tcl_Interp *interp);

void
DciEvalLibInit(void)
{
    DciAddIdent(rcsid);
}


static void
FreeProcs(ClientData arg, Tcl_Interp *interp)
{
    ns_free(arg);
}


static Procs *
GetProcs(Tcl_Interp *interp)
{
    Procs *procsPtr;
    Tcl_CmdProc *proc;
    ClientData data;

    procsPtr = Tcl_GetAssocData(interp, key, NULL);
    if (procsPtr == NULL) {
    	procsPtr = ns_calloc(1, sizeof(Procs));
        if (DciGetCommandInfo(interp, "ns_adp_eval", &proc, &data)) {
	    procsPtr->evalProc = proc;
	    procsPtr->evalData = data;
	}
        if (DciGetCommandInfo(interp, "ns_adp_puts", &proc, &data)) {
	    procsPtr->putsProc = proc;
	    procsPtr->putsData = data;
	}
        Tcl_SetAssocData(interp, key, FreeProcs, procsPtr);
    }
    return procsPtr;
}


int
Dci_AdpPuts(Tcl_Interp *interp, char *html)
{
    Procs *procsPtr = GetProcs(interp);
    char *argv[3];

    if (procsPtr->putsProc == NULL) {
	Tcl_SetResult(interp, "no ns_adp_puts command", TCL_STATIC);
	return TCL_ERROR;
    }
    argv[0] = "ns_puts";
    argv[1] = html;
    argv[2] = NULL;
    return (*procsPtr->putsProc)(procsPtr->putsData, interp, 2, argv);
}


int
Dci_AdpSafeEval(Tcl_Interp *interp, char *script)
{
    return Dci_AdpEval(interp, script);
}


int
Dci_AdpEval(Tcl_Interp *interp, char *script)
{
    Procs *procsPtr = GetProcs(interp);
    char *argv[3];

    if (procsPtr->evalProc == NULL) {
	Tcl_SetResult(interp, "no ns_adp_eval command", TCL_STATIC);
	return TCL_ERROR;
    }
    argv[0] = "<inline>";
    argv[1] = script;
    argv[2] = NULL;
    return (*procsPtr->evalProc)(procsPtr->evalData, interp, 2, argv);
}
