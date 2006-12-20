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

#define PAGE_ARRAY	"_dcipage_info"

static Tcl_CmdProc PageSetCmd, PageGetCmd, PageAppendCmd;

void
DciPageLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciPageTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "page.setValue", PageSetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "page.getValue", PageGetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "page.appendValue", PageAppendCmd, NULL, NULL);
    return TCL_OK;
}


char *
Dci_GetPageVar(Tcl_Interp *interp, char *varName)
{
    return Tcl_GetVar2(interp, PAGE_ARRAY, varName, TCL_GLOBAL_ONLY);
}


char *
Dci_SetPageVar(Tcl_Interp *interp, char *varName, char *value)
{
    return Tcl_SetVar2(interp, PAGE_ARRAY, varName, value, TCL_GLOBAL_ONLY);
}


static int
PageSetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " property value\"", NULL);
	return TCL_ERROR;
    }
    Dci_SetPageVar(interp, argv[1], argv[2]);
    return TCL_OK;
}


static int
PageGetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *value;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " property\"", NULL);
	return TCL_ERROR;
    }

    value = Dci_GetPageVar(interp, argv[1]);
    if (value != NULL) {
    	Tcl_SetResult(interp, value, TCL_VOLATILE);
    }
    return TCL_OK;
}


static int
PageAppendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_DString(ds);
    char *value;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " property value\"", NULL);
	return TCL_ERROR;
    }

    value = Dci_GetPageVar(interp, argv[1]);

    Tcl_DStringInit(&ds);
    if (value != NULL) {
        Tcl_DStringAppend(&ds, value, -1);
    }
    Tcl_DStringAppend(&ds, argv[2], -1);
    Dci_SetPageVar(interp, argv[1], ds.string);
    Tcl_DStringFree(&ds);

    return TCL_OK;
}
