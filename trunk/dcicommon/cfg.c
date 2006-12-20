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

static Ns_TclInterpInitProc AddCmds;
static Tcl_CmdProc CfgCmd, ConfigCmd;


/*
 *----------------------------------------------------------------------
 *                                                                      
 * DciCfgInit --                                                        
 *              
 *      Initialize config hashtables based on AOLserver configuration.
 *                                                           
 * Results:                                                  
 *      Standard AOLserver return code.
 *         
 * Side effects:
 *      Creation of config tables.
 *                              
 *----------------------------------------------------------------------
 */
int
DciCfgInit(char *server, char *module)
{
    char    *path, *app, *key, *val;
    Ns_Set  *sPtr, *sPtr2;
    int     i, j, new;
    Tcl_HashTable   *cfgPtr;
    Tcl_HashEntry   *hPtr;
    Tcl_HashTable   *htPtr;
    
    Dci_LogIdent(module, rcsid);
    
    cfgPtr = ns_malloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(cfgPtr, TCL_STRING_KEYS);
    
    path = Ns_ConfigGetPath(server, module, "configs", NULL);
    sPtr = Ns_ConfigGetSection(path);
    for (i = 0; sPtr != NULL && i < Ns_SetSize(sPtr); i++) {
        app = Ns_SetKey(sPtr, i);
        if (app == NULL) {
            Ns_Log(Error, "NULL key in configs section");
            return NS_ERROR;
        }
        hPtr = Tcl_CreateHashEntry(cfgPtr, app, &new);
        if (!new) {
            Ns_Log(Error, "duplicate key(%s) in configs", app);
            return NS_ERROR;
        }
        htPtr = ns_malloc(sizeof(*htPtr));
        Tcl_InitHashTable(htPtr, TCL_STRING_KEYS);
        Tcl_SetHashValue(hPtr, (ClientData) htPtr);
        
        path = Ns_ConfigGetPath(server, module, "config", app, NULL);
        sPtr2 = Ns_ConfigGetSection(path);
        for (j = 0; sPtr2 != NULL && j < Ns_SetSize(sPtr2); j++) {
            key = Ns_SetKey(sPtr2, j);
            val = Ns_SetValue(sPtr2, j);
            if (key == NULL || val == NULL) {
                Ns_Log(Error, "config section(%s) contains NULL key/val", app);
                return NS_ERROR;
            }
            hPtr = Tcl_CreateHashEntry(htPtr, key, &new);
            if (!new) {
                Ns_Log(Error, "config section(%s) contains duplicate key(%s)", app, key);
                return NS_ERROR;
            }
            Tcl_SetHashValue(hPtr, (ClientData) val);
        }
    }
    
    Ns_TclInitInterps(server, AddCmds, cfgPtr);
    
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *                                                                      
 * AddCmds --                                                           
 *           
 *      Ns_TclInitInterps() callback to add Tcl commands.
 *                                                       
 * Results:                                              
 *      Standard Tcl result code.
 *                               
 * Side effects:                 
 *      Tcl commands are added to the master interpreter procedure
 *      table.                                                    
 *                                                                
 *----------------------------------------------------------------------
 */  

static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "cfg.get", CfgCmd, arg, NULL);
    Tcl_CreateCommand(interp, "dci.config", ConfigCmd, arg, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *                                                                      
 * CfgCmd --                                                       
 *          
 *      Tcl interface for access to config file generated hash tables.
 *                                               
 * Results:                                      
 *      Standard Tcl result code.
 *                               
 * Side effects:                 
 *      None.   
 *              
 *----------------------------------------------------------------------
 */
static int
CfgCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashTable   *cfgPtr = arg;
    Tcl_HashTable   *htPtr;
    Tcl_HashEntry   *hPtr;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " app key\"", NULL);
        return TCL_ERROR;                                      
    }
    
    hPtr = Tcl_FindHashEntry(cfgPtr, argv[1]);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp, "config missing data for app: ", argv[1], NULL);
        return TCL_ERROR;
    }
    htPtr = (Tcl_HashTable *) Tcl_GetHashValue(hPtr);
    hPtr = Tcl_FindHashEntry(htPtr, argv[2]);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp,"no such key(", argv[2], ") for config section ",
             argv[1], NULL);
        return TCL_ERROR;
    }
    
    Tcl_SetResult(interp, (char *) Tcl_GetHashValue(hPtr), TCL_STATIC);
    
    return TCL_OK;
}


static int
ConfigCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *value;
    char path[255];

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " section key ?default?\"", NULL);
	return TCL_ERROR;
    }
    sprintf(path, "dci/%s", argv[1]);
    value = Ns_ConfigGetValue(path, argv[2]);
    if (value == NULL && argv[3] != NULL) {
	value = argv[3];
    }
    Tcl_SetResult(interp, value, TCL_VOLATILE);
    return TCL_OK;
}
