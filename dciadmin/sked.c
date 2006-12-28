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

#include "dciadmin.h"

static Dci_Que *skedQue;

int
DciSkedInit(char *server, char *module)
{
    static char script[] = "sked.run";

    skedQue = DciQueCreate(server, "-sked-", script);
    return NS_OK;
}


int
Dci_SkedNextCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    int time;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " time\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &time) != TCL_OK) {
	return TCL_ERROR;
    }
    DciQueNext(skedQue, time);
    return TCL_OK;
}
