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

/*
 * CB structures.
 */

typedef struct {
    char *name;
    char *proc;
    int maxposts;
    Ns_Cache *cache;
} Cb;

typedef struct {
    Tcl_HashTable table;
    int debug;
} CbServ;

static int CbGetFile(Tcl_Interp *interp, Cb *cbPtr, char *key, Ns_DString *dsPtr);
static int CbPutFile(Tcl_Interp *interp, Cb *cbPtr, char *key, char *data) ;
static int CbGetObj(CbServ *sPtr, Tcl_Interp *interp, char *name, Cb **cbPtrPtr);
static int CbUpdate(CbServ *sPtr, Tcl_Interp *interp, int delete, char **argv);
static void CbFree(void *arg);

static Tcl_CmdProc CbGetCmd;
static Tcl_CmdProc CbPostCmd;
static Tcl_CmdProc CbDeleteCmd;
static Tcl_CmdProc CbCreateCmd;
static Ns_TclInterpInitProc AddCmds;


/*
 *----------------------------------------------------------------------
 *
 * DciCbInit --
 *
 *  	Initialize the Tcl CB interface.
 *
 * Results:
 *      Standard AOLserver module load result.
 *
 * Side effects:
 *  	Commands added.
 *
 *----------------------------------------------------------------------
 */

int
DciCbInit(char *server, char *module)
{
    CbServ *sPtr;

    Dci_LogIdent(module, rcsid);
    sPtr = ns_malloc(sizeof(CbServ));
    sPtr->debug = 0;
    Tcl_InitHashTable(&sPtr->table, TCL_STRING_KEYS);
    Ns_TclInitInterps(server, AddCmds, sPtr);
    return NS_OK;
}    
 
static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    CbServ *sPtr = arg;

    Tcl_CreateCommand(interp, "cb.get", CbGetCmd, arg, NULL);
    Tcl_CreateCommand(interp, "cb.post", CbPostCmd, arg, NULL);
    Tcl_CreateCommand(interp, "cb.delete", CbDeleteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "cb.create", CbCreateCmd, arg, NULL);
    Tcl_CreateCommand(interp, "cb.debug", DciSetDebugCmd, &sPtr->debug, NULL);
    return TCL_OK;
}    


/*
 *----------------------------------------------------------------------
 *
 * CbCreateCmd --
 *
 *  	Create a CB object.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Given proc will be evaluated when a board needs to be 
 *	read or written.
 *
 *----------------------------------------------------------------------
 */

static int
CbCreateCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    CbServ *sPtr = arg;
    Cb *cbPtr;
    Tcl_HashEntry *hPtr;
    int i, new, maxposts, cachesize, *iPtr;
    Ns_Cache *cache;
    Ns_DString ds;
    char *name, *proc;

    if (argc < 3 || !(argc % 2)) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " name proc ?-opt val -opt val ...?\"", NULL);
	return TCL_ERROR;
    }
    name = argv[1];
    proc = argv[2];
    maxposts = 500;
    cachesize = 5*1000*1024;
    for (i = 3; i < argc; i += 2) {
	if (STREQ(argv[i], "-maxposts")) {
	    iPtr = &maxposts;
	} else if (STREQ(argv[i], "-cachesize")) { 
	    iPtr = &cachesize;
	} else {
	    Tcl_AppendResult(interp, "unknown option \"",
		argv[i], "\": should be -maxposts or -cachesize", NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, argv[i+1], iPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (*iPtr <= 0) {
	    Tcl_AppendResult(interp, "invalid ", argv[i], " option: ", argv[i+1], NULL);
	    return TCL_ERROR;
	}
    }
    hPtr = Tcl_CreateHashEntry(&sPtr->table, name, &new);
    if (!new) {
	Tcl_AppendResult(interp, "already exists: ", name, NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "cb:", name, NULL);
    cache = Ns_CacheCreateSz(ds.string, TCL_STRING_KEYS, (size_t)cachesize, CbFree);
    if (cache == NULL) {
	Tcl_AppendResult(interp, "could not create cache: ", ds.string, NULL);
	Tcl_DeleteHashEntry(hPtr);
    } else {
    	cbPtr = ns_malloc(sizeof(Cb));
    	cbPtr->name = ns_strdup(name);
    	cbPtr->proc = ns_strdup(proc);
    	cbPtr->cache = cache;
    	cbPtr->maxposts = maxposts;
   	Tcl_SetHashValue(hPtr, cbPtr);
    }
    Ns_DStringFree(&ds);
    return (cache ? TCL_OK : TCL_ERROR);
}


/*
 *----------------------------------------------------------------------
 *
 * CbPostCmd --
 *
 *  	Invoke the callback to post a message.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *  	Current cached entry, if any, is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
CbPostCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    CbServ *sPtr = arg;

    if (argc < 5 || argc > 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
    	    argv[0], " server board user message ?fields?\"", NULL);
	return TCL_ERROR;
    }
    return CbUpdate(sPtr, interp, 0, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * CbDeleteCmd --
 *
 *  	Invoke the callback to delete a message.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *  	Current cached entry, if any, is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
CbDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    CbServ *sPtr = arg;

    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
    	    argv[0], " server board msgId\"", NULL);
	return TCL_ERROR;
    }
    return CbUpdate(sPtr, interp, 1, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * CbGetCmd --
 *
 *  	Fetch requested messages from a board.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *  	Parsed board is cached on first access.
 *
 *----------------------------------------------------------------------
 */

static int
CbGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    CbServ *sPtr = arg;
    Ns_Entry *entry;
    Ns_DString ds;
    Dci_Board *board;
    char *msgs, *key;
    int len, nposts, new, first, last;
    Cb *cbPtr;

    if (argc < 3 || argc > 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server board ?first? ?last? ?totalVar?\"", NULL);
	return TCL_ERROR;
    }
    if (CbGetObj(sPtr, interp, argv[1], &cbPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc < 4) {
	first = 0;
    } else if (Tcl_GetInt(interp, argv[3], &first) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc < 5) {
	last = cbPtr->maxposts;
    } else if (Tcl_GetInt(interp, argv[4], &last) != TCL_OK) {
	return TCL_ERROR;
    }
    key = argv[2];
    msgs = NULL;
    board = NULL;

    /*
     * Find or create the cache entry.
     */
     
    Ns_CacheLock(cbPtr->cache);
    entry = Ns_CacheCreateEntry(cbPtr->cache, key, &new);
    if (!new) {
	/*
	 * Cache hit:  Get existing data, waiting for pending update or failure
	 * in another thread if necessary.
	 */

    	while (entry != NULL && (board = Ns_CacheGetValue(entry)) == NULL) {
	    Ns_CacheWait(cbPtr->cache);
	    entry = Ns_CacheFindEntry(cbPtr->cache, key);
	}

    } else {
    	/*
	 * Cache miss:  Invoke proc to read msgs.
	 */

    	Ns_CacheUnlock(cbPtr->cache);
    	Ns_DStringInit(&ds);
        len = 0; /* default this to quiet compiler */
	if (CbGetFile(interp, cbPtr, key, &ds)) {
	    len = ds.length;
	    board = Dci_CbParse(ds.string);
	}
    	Ns_DStringFree(&ds);
	Ns_CacheLock(cbPtr->cache);

    	/*
	 * If the fetch suceeded, update the value. Otherwise, flush
	 * the entry.
	 */

	entry = Ns_CacheCreateEntry(cbPtr->cache, key, &new);
	if (board != NULL) {
	    Ns_CacheSetValueSz(entry, board, (size_t)len);
	} else {
	    Ns_CacheFlushEntry(entry);
	}
	Ns_CacheBroadcast(cbPtr->cache);
    }

    /*
     * If data was fetched and/or located in cache, transfer to
     * this thread.
     */

    if (board != NULL) {
	msgs = Dci_CbGet(board, first, last, &nposts);
    }
    Ns_CacheUnlock(cbPtr->cache);

    if (sPtr->debug) {
	Ns_Log(msgs ? Notice : Warning, "%s: get %s - %s%s",
	    cbPtr->name, key,
	    msgs ? "ok" : "failed", new ? "" : " (cached)");
    }
    if (msgs == NULL) {
	return TCL_ERROR;
    }
    if (argc == 6) {
	char buf[20];
    	sprintf(buf, "%d", nposts);
    	if (Tcl_SetVar(interp, argv[5], buf, TCL_LEAVE_ERR_MSG) == NULL) {
	    if (msgs != NULL) {
	    	ns_free(msgs);
	    }
	    return TCL_ERROR;
	}
    }
    Tcl_SetResult(interp, msgs, (Tcl_FreeProc *) ns_free);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CbUpdate --
 *
 *  	Update a board, either deleting a message or posting a new one.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Given proc will be evaluated to write board.
 *
 *----------------------------------------------------------------------
 */

static int
CbUpdate(CbServ *sPtr, Tcl_Interp *interp, int delete, char **argv)
{
    Cb *cbPtr;
    Ns_DString ds;
    Ns_Entry *entry;
    int id, status;
    char *board;

    if (delete && Tcl_GetInt(interp, argv[3], &id) != TCL_OK) {
	return TCL_ERROR;
    }
    if (CbGetObj(sPtr, interp, argv[1], &cbPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    board = argv[2];
    status = NS_ERROR;
    Ns_DStringInit(&ds);
    Ns_CacheLock(cbPtr->cache);
    entry = Ns_CacheFindEntry(cbPtr->cache, board);
    if (entry != NULL) {
	Ns_CacheUnsetValue(entry);
    }
    Ns_CacheUnlock(cbPtr->cache);
    if (CbGetFile(interp, cbPtr, board, &ds)) {
	if (delete) {
	    status = Dci_CbDelete(board, &ds, id);
	} else {
	    status = Dci_CbPost(board, &ds, argv[3], argv[4], argv[5], cbPtr->maxposts);
	}
	if (status == NS_OK && !CbPutFile(interp, cbPtr, board, ds.string)) {
	    status = NS_ERROR;
	}
    }
    Ns_CacheLock(cbPtr->cache);
    entry = Ns_CacheFindEntry(cbPtr->cache, board);
    if (entry != NULL) {
	Ns_CacheFlushEntry(entry);
	Ns_CacheBroadcast(cbPtr->cache);
    }
    Ns_CacheUnlock(cbPtr->cache);
    Ns_DStringFree(&ds);
    return (status == NS_OK ? TCL_OK : TCL_ERROR);
}


/*
 *----------------------------------------------------------------------
 *
 * CbGetObj --
 *
 *  	Find the given CB object.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
CbGetObj(CbServ *sPtr, Tcl_Interp *interp, char *name, Cb **cbPtrPtr)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&sPtr->table, name);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such board server: ", name, NULL);
	return TCL_ERROR;
    }
    *cbPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CbEval, CbGetFile, CbPutFile --
 *
 *  	Evaluate the CB proc to read or write a board.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Board may be written in some manner by CB proc.
 *
 *----------------------------------------------------------------------
 */

static int
CbEval(Tcl_Interp *interp, Cb *cbPtr, char *key, char *data)
{
    Tcl_DString script;
    int result;

    Tcl_DStringInit(&script);
    Tcl_DStringAppendElement(&script, cbPtr->proc);
    Tcl_DStringAppendElement(&script, cbPtr->name);
    Tcl_DStringAppendElement(&script, data ? "put" : "get");
    Tcl_DStringAppendElement(&script, key);
    Tcl_DStringAppendElement(&script, data ? data : "");
    result = Tcl_Eval(interp, script.string);
    Tcl_DStringFree(&script);
    return (result == TCL_OK ? 1 : 0);
}

static int
CbGetFile(Tcl_Interp *interp, Cb *cbPtr, char *key, Ns_DString *dsPtr)
{
    if (!CbEval(interp, cbPtr, key, NULL)) {
	return 0;
    }
    Ns_DStringAppend(dsPtr, interp->result);
    Tcl_ResetResult(interp);
    return 1;
}

static int
CbPutFile(Tcl_Interp *interp, Cb *cbPtr, char *key, char *data) 
{
    return CbEval(interp, cbPtr, key, data);
}


/*
 *----------------------------------------------------------------------
 *
 * CbFree --
 *
 *  	Cache callback to free a parsed board.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
static void
CbFree(void *arg)
{
    Dci_Board *board = arg;

    Dci_CbFree(board);
}
