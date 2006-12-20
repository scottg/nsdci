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

static Tcl_AppInitProc AppInit;
static Tcl_AppInitProc *initProc;


void
Dci_Main(int argc, char **argv, Tcl_AppInitProc *proc)
{
    char name[30], *exec;
    int proxy;

    DciAddIdent(rcsid);
    Tcl_FindExecutable(argv[0]);
    if (argc > 2 && STREQ(argv[1], "-P")) {
	proxy = 1;
    } else {
	proxy = 0;
    }
    exec = strrchr(argv[0], '/');
    if (exec != NULL) {
	++exec;
    } else {
	exec = argv[0];
    }
    sprintf(name, "%.20s:%s", exec, proxy ? "proxy" : "shell");
    Ns_ThreadSetName(name);
    initProc = proc;
    if (proxy) {
	DciProxyMain(argc, argv);
    } else {
    	Tcl_Main(argc, argv, AppInit);
    }
    Tcl_Exit(0);
}


static int
AppInit(Tcl_Interp *interp)
{
    if (Tcl_Init(interp) != TCL_OK ||
	Ns_TclInit(interp) != TCL_OK ||
	DciAppInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}


int
DciAppInit(Tcl_Interp *interp)
{
    if (Dci_Init(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    if ((initProc != NULL && (*initProc)(interp) != TCL_OK)) { 
	return TCL_ERROR;
    }
    return TCL_OK;
}
