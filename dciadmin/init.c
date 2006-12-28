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

static int AddCmds(Tcl_Interp *interp, void *context);

extern int DciQueFileInit(char *, char *);
extern int DciQueInit(char *, char *);
extern int DciSkedInit(char *, char *);

DllExport int
Ns_ModuleInit(char *server, char *module)
{
    Ns_TclInitInterps(server, AddCmds, NULL);

    if (DciQueFileInit(server, module) != NS_OK ||
    	DciQueInit(server, module) != NS_OK ||
    	DciSkedInit(server, module) != NS_OK) {
    	return NS_ERROR;
    }

    return NS_OK;
}

static int
AddCmds(Tcl_Interp *interp, void *context) 
{
    /*
     * que.c
     */
    Tcl_CreateCommand(interp, "que.next", Dci_QueNextCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "que.send", Dci_QueSendCmd, NULL, NULL);

    /*
     * quefile.c
     */
    Tcl_CreateCommand(interp, "qf.getAll", Dci_QfGetAllCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "qf.getFirst", Dci_QfGetFirstCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "qf.getDir", Dci_QfGetDirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "qf.insert", Dci_QfInsertCmd, NULL, NULL);

    /*
     * sked.c
     */
    Tcl_CreateCommand(interp, "sked.next", Dci_SkedNextCmd, NULL, NULL);

    /*
     * links.c
     */
    Tcl_CreateCommand(interp, "links.get", Dci_GetLinksCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "links.write", Dci_WriteLinksCmd, NULL, NULL);

    /*
     * csv.c
     */
    Tcl_CreateCommand(interp, "dci.csvGet", Dci_CsvGetCmd, NULL, NULL);

    return NS_OK;
}
