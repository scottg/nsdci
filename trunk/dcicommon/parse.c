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

static Tcl_CmdProc GetProcsCmd;

void
DciParseLibInit(void)
{
    DciAddIdent(rcsid);
}

int
DciParseTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.getprocs", GetProcsCmd, NULL, NULL);
    return TCL_OK;
}

static int
GetProcsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Parse parse;
    Tcl_Obj *cmdPtr, *valsPtr[2];
    char *p, *vars[2], *next, *script;
    char  err[100];
    int   i, n, len;

    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", 
		argv[0], " script initVar procVar\"", NULL);
	return TCL_ERROR;
    }
    p = argv[1];
    n = strlen(p);

    /*
     * Get the current values of init and procs vars, if any.
     */

    argv += 2;
    argc -= 2;
    for (i = 0; i < argc; ++i) {
	vars[i] = argv[i];
	valsPtr[i] = Tcl_GetVar2Ex(interp, vars[i], NULL, 0);
    }

    /*
     * Parse and append procs and non-proc command to script vars.
     */

    do {
	if (Tcl_ParseCommand(interp, p, n, 0, &parse) != TCL_OK) {
	    sprintf(err, "\n    (script offset %d)", p - argv[1]);
	    Tcl_AddErrorInfo(interp, err);
	    return TCL_ERROR;
	}
	if (parse.numWords > 0) {
	    if (strncmp(parse.tokenPtr->start, "proc", 4) != 0) {
		i = 0;	/* NB: Append init var. */
	    } else {
		i = 1;	/* NB: Append proc var. */
	    }

	    /*
	     * Check that previous script value is newline terminated
	     * before appending the next command.
	     */

	    if (valsPtr[i] != NULL) {
    	    	script = Tcl_GetStringFromObj(valsPtr[i], &len);
    	    	if (len > 0 && script[len-1] != '\n'
		    	&& Tcl_SetVar2(interp, vars[i], NULL, "\n",
			     TCL_LEAVE_ERR_MSG | TCL_APPEND_VALUE) == NULL) {
		    return TCL_ERROR;
		}
	    }
	    cmdPtr = Tcl_NewStringObj(parse.commandStart, parse.commandSize);
	    Tcl_IncrRefCount(cmdPtr);
	    valsPtr[i] = Tcl_SetVar2Ex(interp, vars[i], NULL, cmdPtr,
				   TCL_APPEND_VALUE|TCL_LEAVE_ERR_MSG);
	    Tcl_DecrRefCount(cmdPtr);
	    if (valsPtr[i] == NULL) {
		return TCL_ERROR;
	    }
	}
	next = parse.commandStart + parse.commandSize;
	n -= next - p;
	p = next;
	Tcl_FreeParse(&parse);
    } while (n > 0);
    return TCL_OK;
}
