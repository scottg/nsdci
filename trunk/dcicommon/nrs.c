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

extern int DciNrsCreate(char *server, char *module, char *name, char *path);
extern char *DciNrsGet(Tcl_Interp *interp, char *key);

static Ns_Callback NrsFreeTable;
static Ns_TraceProc NrsFlushTable;

static char *NrsGetFile(Tcl_Interp *interp, char *key, int *cachedPtr);

static Tcl_CmdProc NrsGetCmd;
static Tcl_CmdProc NrsGetFieldCmd;
static Tcl_CmdProc NrsGetTitleCmd;
static Tcl_CmdProc NrsGetTextCmd;
static Ns_TclInterpInitProc AddCmds;

static Ns_Tls nrsTls;
static int fNrsEval;
static int fDebug;


/*
 *----------------------------------------------------------------------
 *
 * DciNrsInit --
 *
 *      Initialize nrs clients and servers based AOLserver configuration
 *      settings. This includes simple file servers, as well as servers
 *      with custom callbacks such as: poll, and comment boards.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Creation of sob servers and/or clients.
 *
 *----------------------------------------------------------------------
 */

int
DciNrsInit(char *server, char *module)
{
    char *path, *name;
    Ns_Set *set;
    int i;
    
    Dci_LogIdent(module, rcsid);

    /*
     * Create the NRS client.
     */
     
    path = Ns_ConfigGetPath(server, module, "nrs", NULL);
    if (path != NULL) {
    	if (!Ns_ConfigGetBool(path, "conncache", &i) || i) {
	    Ns_TlsAlloc(&nrsTls, NrsFreeTable);
	    Ns_RegisterConnCleanup(server, NrsFlushTable, NULL);
	}
    	if (!Ns_ConfigGetBool(path, "texteval", &fNrsEval)) {
	    fNrsEval = 1;
	}
	if (DciNrsCreate(server, module, "nrs", path) != NS_OK) {
	    return NS_ERROR;
	}
    	Ns_TclInitInterps(server, AddCmds, NULL);
    }
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
    Tcl_CreateCommand(interp, "nrs.get", NrsGetCmd, (ClientData) 'p', NULL); 
    Tcl_CreateCommand(interp, "nrs.getcol", NrsGetCmd, (ClientData) 'c', NULL);
    Tcl_CreateCommand(interp, "nrs.getlink", NrsGetCmd, (ClientData) 'l', NULL); 
    Tcl_CreateCommand(interp, "nrs.getCol", NrsGetCmd, (ClientData) 'c', NULL);
    Tcl_CreateCommand(interp, "nrs.getFile", NrsGetCmd, (ClientData) 'f', NULL);
    Tcl_CreateCommand(interp, "nrs.getField", NrsGetFieldCmd, NULL, NULL); 
    Tcl_CreateCommand(interp, "nrs.getTitle", NrsGetTitleCmd, NULL, NULL); 
    Tcl_CreateCommand(interp, "nrs.getText", NrsGetTextCmd, NULL, NULL); 
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsGetCmd --
 *
 *      Tcl interface for NrsGetFile(). NrsGetCmd determines the
 *      path to the sob file based on ClientData, defined in AddCmds().
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
NrsGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    char *value, *data;
    int type = (int) arg;
    int result, cached;
    Ns_DString key;
    char *keyref;

    result = TCL_ERROR;
    Ns_DStringInit(&key);

    /*
     * Construct the full pathname based on type of call and
     * args.
     */

    switch (type) {
    case 'p': 
	if (argc != 5 && argc != 6) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
	        argv[0], " server area page field ?version?\"", NULL);
	    goto badargs;
	}
        Ns_MakePath(&key, "pub/", argv[1], argv[2], argv[3],
	    "versions", argv[5] ? argv[5] : "current", "data.dat", NULL);
	break;

    case 'c':
    	if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
	        argv[0], " server area page\"", NULL);
	    goto badargs;
	}
        Ns_MakePath(&key, "pub/", argv[1], argv[2], argv[3], "collection.dat", NULL);
	break;

    case 'f':
    	if (argc != 5) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
	        argv[0], " server area page file\"", NULL);
	    goto badargs;
	}
        Ns_MakePath(&key, "pub/", argv[1], argv[2], argv[3], argv[4], NULL);
	break;

    case 'l':
    	if (argc != 5) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
	        argv[0], " server area page link\"", NULL);
	    goto badargs;
	}
        Ns_MakePath(&key, "pub/", argv[1], argv[2], argv[3], "links.dat", NULL);
	break;
    }

    /*
     * Fetch contents through per-thread and/or global cache and
     * scan for requested fields.
     */

    result = TCL_OK;

    /*
     * The standard for nrs key names is that they do not have a leading
     * '/'.  Depending upon the version of aolserver in use, Ns_MakePath
     * may insert a '/' at the front of the result string.  This needs
     * to be adjusted for, to keep this behavior consistent.
     */
    keyref = key.string;
    if (keyref[0] == '/') {
        keyref++;
    }

    data = NrsGetFile(interp, keyref, &cached);
    if (data != NULL) {
    	if ((type == 'l' || type == 'p') && argv[4][0] != '\0') {
	    result = Tcl_GetKeyedListField(interp, argv[4], data, &value);
	    if (result == TCL_OK) {
            	Tcl_SetResult(interp, value, TCL_DYNAMIC);
	    }
	} else {
	    Tcl_SetResult(interp, data, TCL_VOLATILE);
	}
    	if (!cached) {
	    ns_free(data);
    	}
    }

badargs:
    Ns_DStringFree(&key);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsGetFieldCmd --
 *
 *      Additional Tcl interface on top of NrsGetCmd(). Arguments are
 *      concatenated with periods to represent a key in TclX keyed
 *      list format (see tclxkeylist.c in this directory.) 
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NrsGetFieldCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    int nargc;
    char *nargv[7];

    if (argc < 5 || argc > 7) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server area page field ?subfield? ?version?\"", NULL);
	return TCL_ERROR;
    }
    for (nargc = 0; nargc < 5; ++nargc) {
	if (argv[nargc][0] == '\0') {
	    return TCL_OK;
	}
	nargv[nargc] = argv[nargc];
    }

    Ns_DStringInit(&ds);
    if (argc > 5) {
	if (argv[5][0] != '\0') {
	    nargv[4] = Ns_DStringVarAppend(&ds, argv[4], ".", argv[5], NULL);
	}
	if (argc > 6) {
	    nargv[nargc++] = argv[6];
	}
    }
    nargv[nargc] = NULL;
    (void) NrsGetCmd((ClientData) 'p', interp, nargc, nargv);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsGetTitleCmd --
 *
 *      Additional Tcl interface on top of NrsGetFieldCmd().
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
NrsGetTitleCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int nargc;
    char *nargv[8];

    if (argc != 4 && argc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server area page ?version?\"", NULL);
	return TCL_ERROR;
    }
    for (nargc = 0; nargc < 4; ++nargc) {
	if (argv[nargc][0] == '\0') {
	    return TCL_OK;
	}
	nargv[nargc] = argv[nargc];
    }
    nargv[nargc++] = "title";
    if (argc == 5) {
    	nargv[nargc++] = "";
    	nargv[nargc++] = argv[4];
    }
    nargv[nargc] = NULL;
    (void) NrsGetFieldCmd(NULL, interp, nargc, nargv);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsGetTextCmd --
 *      Additional Tcl interface on top of NrsGetFieldCmd().
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      If fNrsEval is set in the server config, Tcl code between "<%"
 *      and "%>" is evaluated using Dci_AdpSafeEval() (see eval.c
 *      in this directory).
 *
 *----------------------------------------------------------------------
 */

static int
NrsGetTextCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int nargc;
    char *nargv[8];
    char *s, *e;

    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server area page field ?version?\"", NULL);
	return TCL_ERROR;
    }
    for (nargc = 0; nargc < 5; ++nargc) {
	if (argv[nargc][0] == '\0') {
	    return TCL_OK;
	}
	nargv[nargc] = argv[nargc];
    }
    nargv[nargc++] = "body";
    if (argc == 6) {
    	nargv[nargc++] = argv[5];
    }
    nargv[nargc] = NULL;
    (void) NrsGetFieldCmd(NULL, interp, nargc, nargv);
    if (fNrsEval && (s = strstr(interp->result, "<%"))
    	    && (e = strstr(s, "%>"))) {
	(void) Dci_AdpSafeEval(interp, interp->result);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsGetFile --
 *
 *      Rosetta based wrapper for SobGetFile().
 *
 * Results:
 *      Pointer to the cached contents of the file.
 *
 * Side effects:
 *      Data retrieved via SobGetFile is stored in a per-thread cache
 *      in order to reduce lock contention. The nrs commands are
 *      typically executed many times for a given nrs file during 
 *      a single connection, so the per-thread cache eliminates 
 *      needless locks around a global hash table.
 *      NB: The per-thread cache is purged at the end of the connection.
 *      See NrsFreeTable() below.
 *
 *----------------------------------------------------------------------
 */

static char *
NrsGetFile(Tcl_Interp *interp, char *key, int *cachedPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    char *data;
    int new;

    data = NULL;
    if (nrsTls == NULL || Ns_TclGetConn(interp) == NULL) {
	data = DciNrsGet(interp, key);
	new = 1;
	*cachedPtr = 0;
    } else {
    	tablePtr = Ns_TlsGet(&nrsTls);
    	if (tablePtr == NULL) {
	    tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	    Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
	    Ns_TlsSet(&nrsTls, tablePtr);
	}
	hPtr = Tcl_CreateHashEntry(tablePtr, key, &new);
	if (!new) {
	    data = Tcl_GetHashValue(hPtr);
	} else {
	    data = DciNrsGet(interp, key);
	    Tcl_SetHashValue(hPtr, data);
	}
	*cachedPtr = 1;
    }
    if (fDebug) {
	Ns_Log(data ? Notice : Warning, "nrs: get %s %s (%s)", key,
	    data ? "ok" : "failed", new ? "new" : "cached");
    }
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * NrsFlushEntries --
 *
 *      Frees hash table entries associated with the per-thread 
 *      nrs cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Free's previously allocated memory.
 *
 *----------------------------------------------------------------------
 */

static void
NrsFlushEntries(Tcl_HashTable *tablePtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *data;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
    	data = Tcl_GetHashValue(hPtr);
	if (data != NULL) {
	    ns_free(data);
	}
	Tcl_DeleteHashEntry(hPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NrsFreeTable --
 *
 *      Destructor function for the per-thread nrs cache created in
 *      thread local starge - see DciSobInit() above.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed for the nrs hash table.
 *
 *----------------------------------------------------------------------
 */

static void
NrsFreeTable(void *arg)
{
    Tcl_HashTable *tablePtr;

    tablePtr = arg;
    NrsFlushEntries(tablePtr);
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NrsFlushTable --
 *
 *      This is a thread cleanup function called at the end of the
 *      connection; the result of a call to Ns_RegisterCleanup()
 *      in DciSobInit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
NrsFlushTable(void *arg, Ns_Conn *conn)
{
    Tcl_HashTable *tablePtr;

    tablePtr = Ns_TlsGet(&nrsTls);
    if (tablePtr != NULL) {
	NrsFlushEntries(tablePtr);
    }
}
