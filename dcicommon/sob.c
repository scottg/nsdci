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
 * SOB flags.
 */

#define SOB_NCB	     1

/*
 * SOB client structure.
 */

typedef struct {
    char name[DCI_RPCNAMESIZE];
    Dci_Rpc *rpc;
    int timeout;
    int flags;
    Ns_Cache *cache;
} Sob;

/*
 * NCB structures.
 */
 
typedef struct {
    int first;
    int last;
    int npost;
} CbArg;

static Sob *SobCreate(char *server, char *module, char *name, char *path,
		      int ncb);
static int SobGetFile(Tcl_Interp *, Sob *, char *key, char **copyPtr,
		      CbArg *cbPtr);
static void SobFlush(Sob *sobPtr, char *key);

static Ns_Callback NcbFreeBoard;

static int SobGetClient(Tcl_Interp *interp, char *name, Sob **sobPtrPtr);
static int SobGetClient2(Tcl_Interp *interp, char *name, int ncb, Sob **sobPtrPtr);

static Tcl_CmdProc NamesCmd;
static Tcl_CmdProc SobGetCmd;
static Tcl_CmdProc SobPutCmd;
static Tcl_CmdProc SobCopyCmd;
static Tcl_CmdProc SobDeleteCmd;
static Tcl_CmdProc NcbGetCmd;
static Tcl_CmdProc NcbPostCmd;
static Tcl_CmdProc NcbDeleteCmd;
static Tcl_CmdProc Ncb2Cmd;
static Ns_TclInterpInitProc AddCmds;

static Tcl_HashTable sobTable;
static int fDebug;


/*
 *----------------------------------------------------------------------
 *
 * DciSobInit --
 *
 *      Initialize sob clients and servers based AOLserver configuration
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
DciSobInit(char *server, char *module)
{
    char *path, *name;
    Ns_Set *set;
    int i, isNcb;
    Sob *ncbPtr = NULL;
    
    Dci_LogIdent(module, rcsid);

    /*
     * Initialize the NFS servers and SOB clients tables and
     * core options.
     */
     
    Tcl_InitHashTable(&sobTable, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "sob", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
	fDebug = 0;
    }
    
    /*
     * Create SOB clients.
     */
     
    path = Ns_ConfigGetPath(server, module, "sob/clients", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
    	name = Ns_SetKey(set, i);
        isNcb = Tcl_StringMatch(name, "ncb[0-9]*");
	path = Ns_ConfigGetPath(server, module, "sob/client", name, NULL);
	if (SobCreate(server, module, name, path, isNcb) == NULL) {
	    return NS_ERROR;
	}
    }

#if 0
    /*
     * Create NCB clients.
     */

    path = Ns_ConfigGetPath(server, module, "ncb/clients", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
    	name = Ns_SetKey(set, i);
	path = Ns_ConfigPath(server, module, "ncb/client", name, NULL);
	if (SobCreate(server, module, name, path, 1) == NULL) {
	    return NS_ERROR;
	}
    }
    /*
     * Create the default NCB client.
     */

    path = Ns_ConfigGetPath(server, module, "ncb", NULL);
    if (path != NULL) {
    	ncbPtr = SobCreate(server, module, "ncb", path, 1);
	if (ncbPtr == NULL) {
	    return NS_ERROR;
	}
    }
#endif
     
    /*
     * Add Tcl commands.
     */

    Ns_TclInitInterps(server, AddCmds, ncbPtr);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_SobRpcName --
 *
 *      Creates an RPC name for the SOB name.  This name is also
 *	used for the cache name.
 *
 * Results:
 *      Pointer to new string.
 *
 * Side effects:
 *      Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

int
Dci_SobRpcName(char *name, char *buf)
{
    return Dci_RpcName("nsobc", name, buf);
}


/*
 *----------------------------------------------------------------------
 *
 * SobCreate --
 *
 *      Creates and initializes a sob client, built on 
 *      Dci_RpcCreateClient() with additional caching.
 *
 * Results:
 *      Pointer to Sob struct.
 *
 * Side effects:
 *      Creates an Ns_Cache, and RpcClient.
 *
 *----------------------------------------------------------------------
 */

static Sob *
SobCreate(char *server, char *module, char *name, char *path, int ncb)
{
    Tcl_HashEntry *hPtr;
    int new, cachesize, timeout, nocache;
    Sob *sobPtr;
    char rpcname[DCI_RPCNAMESIZE];
    Ns_Callback *freeProc;
    
    /*
     * Verify options.
     */

    if (Dci_SobRpcName(name, rpcname) != NS_OK) {
	return NULL;
    }
    if (!Ns_ConfigGetBool(path, "nocache", &nocache)) {
	nocache = 0;
    }
    if (ncb && nocache) {
    	Ns_Log(Error, "sob: ncb sob cannot be set to nocache: %s", name);
	return NULL;
    }
    if (!Ns_ConfigGetInt(path, "cachesize", &cachesize)) {
    	cachesize = 5*1024*1024;
    }
    if (!Ns_ConfigGetInt(path, "timeout", &timeout) || timeout < 1) {
    	timeout = 2;
    }

    /*
     * Create and hash the SOB client.
     */

    hPtr = Tcl_CreateHashEntry(&sobTable, name, &new);
    if (!new) {
    	Ns_Log(Error, "sob: already exists: %s", name);
	return NULL;
    }
    sobPtr = ns_malloc(sizeof(Sob));
    strcpy(sobPtr->name, name);
    sobPtr->timeout = timeout;
    sobPtr->flags = 0;
    if (!ncb) {
	freeProc = ns_free;
    } else {
	sobPtr->flags |= SOB_NCB;
	freeProc = NcbFreeBoard;
    }
    if (nocache) {
	sobPtr->cache = NULL;
    } else {
    	sobPtr->cache = Ns_CacheCreateSz(rpcname, TCL_STRING_KEYS,
	    (size_t)cachesize, freeProc);
    }
    sobPtr->rpc = Dci_RpcCreateClient(server, module, rpcname, timeout);
    if (sobPtr->rpc != NULL) {
	Tcl_SetHashValue(hPtr, sobPtr);
    } else {
	Tcl_DeleteHashEntry(hPtr);
	if (sobPtr->cache != NULL) {
    	    Ns_CacheDestroy(sobPtr->cache);
	}
	ns_free(sobPtr);
	sobPtr = NULL;
    }
    Ns_Log(Notice, "sob[%s] created", name);
    return sobPtr;
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
    Sob *ncbPtr = arg;

    /*
     * Add the basic SOB and NCB client commands.
     */
     
    Tcl_CreateCommand(interp, "nsob.names", NamesCmd, (ClientData) 's', NULL);
    Tcl_CreateCommand(interp, "nsob.get", SobGetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nsob.put", SobPutCmd, (ClientData) DCI_NFSCMDPUT, NULL);
    Tcl_CreateCommand(interp, "nsob.append", SobPutCmd, (ClientData) DCI_NFSCMDAPPEND, NULL);
    Tcl_CreateCommand(interp, "nsob.copy", SobCopyCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nsob.delete", SobDeleteCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nsob.debug", DciSetDebugCmd, &fDebug, NULL);

    Tcl_CreateCommand(interp, "ncb2.names", NamesCmd, (ClientData) 'c', NULL);
    Tcl_CreateCommand(interp, "ncb2.get", Ncb2Cmd, NcbGetCmd, NULL);
    Tcl_CreateCommand(interp, "ncb2.post", Ncb2Cmd, NcbPostCmd, NULL);
    Tcl_CreateCommand(interp, "ncb2.delete", Ncb2Cmd, NcbDeleteCmd, NULL);
    Tcl_CreateCommand(interp, "ncb2.debug", DciSetDebugCmd, &fDebug, NULL);

    /*
     * Add the unnamed NCB commands if configured.
     */

    if (ncbPtr != NULL) {
	Tcl_CreateCommand(interp, "ncb.get", NcbGetCmd, ncbPtr, NULL);
	Tcl_CreateCommand(interp, "ncb.post", NcbPostCmd, ncbPtr, NULL);
	Tcl_CreateCommand(interp, "ncb.delete", NcbDeleteCmd, ncbPtr, NULL);
    }    

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NamesCmd --
 *
 *      Tcl interface for the nsob and ncb.names commands.
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
NamesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *pattern, *key;
    Sob *sobPtr;
    int type, etype;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?pattern?\n", NULL);
	return TCL_ERROR;
    }
    pattern = argv[1];
    type = (int) arg;
    hPtr = Tcl_FirstHashEntry(&sobTable, &search);
    while (hPtr != NULL) {
	sobPtr = Tcl_GetHashValue(hPtr);
	etype = (sobPtr->flags & SOB_NCB) ? 'c' : 's';
	key = Tcl_GetHashKey(&sobTable, hPtr);
	if (type == etype && (pattern == NULL || Tcl_StringMatch(key, pattern))) {
	    Tcl_AppendElement(interp, key);
	}
    	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SobGetCmd --
 *
 *      Tcl interface for SobGetFile().
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
SobGetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Sob *sobPtr;

    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server key\"", NULL);
	return TCL_ERROR;
    }
    if (SobGetClient(interp, argv[1], &sobPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    return SobGetFile(interp, sobPtr, argv[2], NULL, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * SobPutCmd --
 *
 *      Send a put or append request to a sob server via Dci_RpcSend().
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Current entry in cache, if any, is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
SobPutCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Sob *sobPtr;
    Dci_RpcJob *jobs;
    Ns_DString ds;
    int i, result, sobc;
    char **sobv;

    if (argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server key data\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_SplitList(interp, argv[1], &sobc, &sobv) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_DStringAppendElement(&ds, argv[2]);
    Ns_DStringAppendElement(&ds, argv[3]);
    jobs = ns_malloc(sizeof(Dci_RpcJob) * sobc);
    for (i = 0; i < sobc; ++i) {
    	if (SobGetClient(interp, sobv[i], &sobPtr) != TCL_OK) {
            result = TCL_ERROR;
	    goto done;
	}
	jobs[i].data = sobPtr;
	jobs[i].rpc = sobPtr->rpc;
	jobs[i].inPtr = &ds;
	jobs[i].outPtr = NULL;
	jobs[i].cmd = (int) arg;
    }
    Dci_RpcRun(jobs, sobc, -1, 0);
    result = TCL_OK;
    for (i = 0; i < sobc; ++i) {
    	if (jobs[i].result != RPC_OK) {
	    Tcl_AppendResult(interp, " could not write: ", sobv[i], 
            	" : ", Dci_RpcTclError(interp, jobs[i].result), NULL);
	    result = TCL_ERROR;
	}
	sobPtr = jobs[i].data;
    	SobFlush(sobPtr, argv[2]);
    }
done:
    ns_free(jobs);
    ckfree((char *) sobv);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * SobCopyCmd --
 *
 *      Send a file copy request to a sob server via Dci_RpcSend().
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Current entry in cache, if any, is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
SobCopyCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Sob *sobPtr;
    Ns_DString ds;
    int namelen, datalen, status, fd, n;
    uint32_t sizes[2];
    struct stat st;
    char *file, *key;

    if (argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file server key\"", NULL);
	return TCL_ERROR;
    }
    if (SobGetClient(interp, argv[2], &sobPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    file = argv[1];
    key = argv[3];
    if (stat(file, &st) != 0) {
	Tcl_AppendResult(interp, "could not stat \"", file,
	    "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    if (!S_ISREG(st.st_mode)) {
	Tcl_AppendResult(interp, "not a regular file: ", file, NULL);
	return TCL_ERROR;
    }
    fd = open(file, O_RDONLY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"", file,
	    "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    namelen = strlen(key);
    datalen = st.st_size;
    sizes[0] = htonl(namelen);
    sizes[1] = htonl(datalen);
    Ns_DStringInit(&ds);
    Ns_DStringNAppend(&ds, (char *) sizes, 8);
    Ns_DStringAppend(&ds, key);
    Ns_DStringSetLength(&ds, namelen + datalen + 8);
    datalen -= read(fd, ds.string+8+namelen, (size_t)datalen);
    close(fd);
    status = TCL_ERROR;
    if (datalen != 0) {
	Tcl_AppendResult(interp, "could not read \"", file,
	    "\": ", Tcl_PosixError(interp), NULL);
    } else {
    	if ((n = Dci_RpcSend(sobPtr->rpc, DCI_NFSCMDCOPY, &ds, NULL)) != RPC_OK) {
	    Tcl_AppendResult(interp, "could not save: ", argv[1], 
                " : ", Dci_RpcTclError(interp, n), NULL);
	} else {
            status = TCL_OK;
	}
    }
    Ns_DStringFree(&ds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * SobDeleteCmd --
 *
 *      Send a "delete" request to a sob server via Dci_RpcSend().
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Current entry in cache, if any, is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
SobDeleteCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Sob *sobPtr;
    Dci_RpcJob *jobs;
    Ns_DString ds;
    int i, result, sobc;
    char **sobv;

    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server key\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_SplitList(interp, argv[1], &sobc, &sobv) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_DStringAppendElement(&ds, argv[2]);
    jobs = ns_malloc(sizeof(Dci_RpcJob) * sobc);
    for (i = 0; i < sobc; ++i) {
    	if (SobGetClient(interp, sobv[i], &sobPtr) != TCL_OK) {
            result = TCL_ERROR;
	    goto done;
	}
	jobs[i].data = sobPtr;
	jobs[i].rpc = sobPtr->rpc;
	jobs[i].inPtr = &ds;
	jobs[i].outPtr = NULL;
	jobs[i].cmd = DCI_NFSCMDUNLINK;
    }
    Dci_RpcRun(jobs, sobc, -1, 0);
    result = TCL_OK;
    for (i = 0; i < sobc; ++i) {
    	if (jobs[i].result != RPC_OK) {
	    Tcl_AppendResult(interp, " could not delete: ", sobv[1], 
            	" : ", Dci_RpcTclError(interp, jobs[i].result), NULL);
	    result = TCL_ERROR;
	}
	sobPtr = jobs[i].data;
    	SobFlush(sobPtr, argv[2]);
    }
done:
    ns_free(jobs);
    ckfree((char *) sobv);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NcbGetCmd --
 *
 *      This is a Tcl interface SobGetFile() for comment boards.
 *	The SOB client to use is passed in the ClientData.
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
NcbGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    CbArg carg;
    char npost[20];
    Sob *sobPtr = arg;

    if (argc < 2 || argc > 5) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " board ?first? ?last? ?totalVar?", NULL);
	return TCL_ERROR;
    }
    carg.first = 0;
    carg.last = INT_MAX;
    if (argc > 2 && Tcl_GetInt(interp, argv[2], &carg.first) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc > 3 && Tcl_GetInt(interp, argv[3], &carg.last) != TCL_OK) {
	return TCL_ERROR;
    }
    if (SobGetFile(interp, sobPtr, argv[1], NULL, &carg) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (argc > 4) {
    	sprintf(npost, "%d", carg.npost);
	if (Tcl_SetVar(interp, argv[4], npost, TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}
    

/*
 *----------------------------------------------------------------------
 *
 * NcbPostCmd --
 *
 *      This is a Tcl interface for Dci_RpcSend() of a DCI_NFSCMDCBPOST
 *      message for comment boards. See Dci_RpcSend() for further
 *      documentation.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      The interp result buffer will be filled with a "1" on success,
 *      "0" on failure. Success means the comment board post was 
 *      recieved by the server (presumably written), and cached entries
 *      were flushed on the client.
 *
 *----------------------------------------------------------------------
 */

static int
NcbPostCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    char *board;
    int status;
    Sob *sobPtr = arg;
    
    if (argc != 4 && argc != 5) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " board user msg ?fields?\"", NULL);
	return TCL_ERROR;
    }
    board = argv[1];
    Ns_DStringInit(&ds);
    Ns_DStringAppendElement(&ds, board);
    Ns_DStringAppendElement(&ds, argv[2]);
    Ns_DStringAppendElement(&ds, argv[3]);
    Ns_DStringAppendElement(&ds, argv[4] ? argv[4] : "");
    status = Dci_RpcSend(sobPtr->rpc, DCI_NFSCMDCBPOST, &ds, NULL);
    Ns_DStringFree(&ds);
    if (status == RPC_OK) {
    	SobFlush(sobPtr, board);
    } else {
        Dci_RpcTclError(interp, status);
    }
    Tcl_SetResult(interp, status == RPC_OK ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NcbDeleteCmd --
 *
 *      This is a Tcl interface for Dci_RpcSend() of a DCI_NFSCMDCBDEL 
 *      message for comment boards. See Dci_RpcSend() for further
 *      documentation.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      The interp result buffer will be filled with a "1" on success,
 *      "0" on failure. Success means the comment board delete was 
 *      recieved by the server (presumably saved), and cached entries
 *      were flushed on the client.
 *
 *----------------------------------------------------------------------
 */

static int
NcbDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    char *board;
    int status, id;
    Sob *sobPtr = arg;
    
    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " board msgId\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &id) != TCL_OK) {
    	return TCL_ERROR;
    }
    board = argv[1];
    Ns_DStringInit(&ds);
    Ns_DStringAppendElement(&ds, board);
    Ns_DStringAppendElement(&ds, argv[2]);
    status = Dci_RpcSend(sobPtr->rpc, DCI_NFSCMDCBDEL, &ds, NULL);
    Ns_DStringFree(&ds);
    if (status == RPC_OK) {
    	SobFlush(sobPtr, board);
    } else {
        Dci_RpcTclError(interp, status);
    }
    Tcl_SetResult(interp, status == RPC_OK ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ncb2Cmd --
 *
 *      Handle ncb2 commands which expect sob server name as first
 *  	argument and then simply call Ncb*Cmd.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      See cooresponding Ncb*Cmd.
 *
 *----------------------------------------------------------------------
 */

static int
Ncb2Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_CmdProc *proc = arg;
    Sob *sobPtr;
    char *nargv[7];
    int i, nargc;

    if (argc < 2 || argc > 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " sob ?...?\"", NULL);
	return TCL_ERROR;
    }
    if (SobGetClient2(interp, argv[1], 1, &sobPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    nargc = 0;
    nargv[nargc++] = argv[0];
    for (i = 2; i < argc; ++i) {
	nargv[nargc++] = argv[i];
    }
    nargv[nargc] = NULL;
    return (*proc)(sobPtr, interp, nargc, nargv);
}


/*
 *----------------------------------------------------------------------
 *
 * SobGetClient, SobGetClient2 --
 *
 *      Find a sob client struct from the global Tcl_HashTable,
 *	verifying it's either an NCB or ordinary SOB client
 *	as requested.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      If the return value is TCL_OK, **sobPtrPtr is filled with the
 *      address of the requested sob struct.
 *
 *----------------------------------------------------------------------
 */

static int
SobGetClient(Tcl_Interp *interp, char *name, Sob **sobPtrPtr)
{
    return SobGetClient2(interp, name, 0, sobPtrPtr);
}

static int
SobGetClient2(Tcl_Interp *interp, char *name, int ncb, Sob **sobPtrPtr)
{
    Tcl_HashEntry *hPtr;
    Sob *sobPtr;
    char *err;

    hPtr = Tcl_FindHashEntry(&sobTable, name);
    if (hPtr == NULL) {
	err = "no such";
    } else {
    	sobPtr = Tcl_GetHashValue(hPtr);
    	if (ncb && !(sobPtr->flags & SOB_NCB)) {
	    err = "not a comment board";
	} else if (!ncb && (sobPtr->flags & SOB_NCB)) {
	    err = "not an ordinary";
	} else {
	    *sobPtrPtr = sobPtr;
	    return TCL_OK;
	}
    }
    Tcl_AppendResult(interp, err, " sob server: ", name, NULL);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * SobFlush --
 *
 *      Removes a cache entry, defined by key, within a sob client's
 *      cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A cache entry is freed, if it exists.
 *
 *----------------------------------------------------------------------
 */

static void
SobFlush(Sob *sobPtr, char *key)
{
    Ns_Entry *entry;
    
    if (sobPtr->cache != NULL) {
    	Ns_CacheLock(sobPtr->cache);
    	entry = Ns_CacheFindEntry(sobPtr->cache, key);
    	if (entry != NULL) {
            Ns_CacheFlushEntry(entry);
    	}
    	Ns_CacheUnlock(sobPtr->cache);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SobGetFile --
 *
 *      This is the heart of the sob client. Files are first checked
 *      in the sob cache, if it does not exist it is requested from
 *      the server via Dci_RpcSend(). After the request, SobGetFile
 *      waits on the cache entry to be filled by the underlying Rpc
 *      mechanisms, for the configured timeout.
 *
 *      Upon retrieval of the file, the data is processed in-line
 *      to be stored in the applications format. Currently, comment
 *      boards are the only application to perform additional
 *      processing, since the list of messages must be reversed for
 *      display in "FILO" order. Data is sent from the
 *      comment board server in "FIFO" order for legacy reasons.
 *
 *      Data is also returned in an application-specific manner. For
 *      generic sob data is copied to the
 *      Tcl interpreters result buffer, however requests
 *      also generate a copy of the data stored in **copyPtr to be used
 *      in a per thread cache. Comment boards store the range of
 *      requested messages (i.e. 1-10 of 23) in the Tcl interpreters
 *      result buffer in proper Tcl list format.
 *
 * Results:
 *      Standard Tcl result code. **copyPtr contains a copy of the
 *      data requested requests, and *cbPtr is updated
 *      on a comment board request.
 *
 * Side effects:
 *      The Tcl interpreter result buffer is updated with a copy
 *      of the cached data on TCL_OK.
 *      
 *      Look to cache.c in the AOLserver nsd directory for more
 *      information on the various cache routines. Also, check
 *      out rpc.c in this directory for information on the Rpc
 *      commands.
 *
 *----------------------------------------------------------------------
 */

static int
SobGetFile(Tcl_Interp *interp, Sob *sobPtr, char *key, char **copyPtr, CbArg *cbPtr)
{
    Ns_Entry *entry;
    Ns_DString in, out;
    char *data;
    int len, cached, new, n;
    Ns_Time timeout;
    
    data = NULL;
    Ns_DStringInit(&in);
    Ns_DStringInit(&out);

    /*
     * If no cache (available only for ordinary SOB), fetch directly and
     * jump to end.
     */

    if (sobPtr->cache == NULL) {
	cached = 0;
	Ns_DStringAppend(&in, key);
	if ((n = Dci_RpcSend(sobPtr->rpc, DCI_NFSCMDREAD, &in, &out)) == RPC_OK) {
	    data = Ns_DStringExport(&out);
	    Tcl_SetResult(interp, data, (Tcl_FreeProc *) ns_free);
	} else {
            Dci_RpcTclError(interp, n);
        }
	goto done;
    }
	
    /*
     * Find or create the cache entry.
     */
     
    Ns_CacheLock(sobPtr->cache);
    entry = Ns_CacheCreateEntry(sobPtr->cache, key, &new);
    if (!new) {
	cached = 1;

	/*
	 * Cache hit:  Get existing data, waiting for pending update or failure
	 * in another thread if necessary.
	 */

	data = Ns_CacheGetValue(entry);
	if (data == NULL) {
	    Ns_GetTime(&timeout);
	    Ns_IncrTime(&timeout, sobPtr->timeout, 0);
    	    while (entry != NULL && (data = Ns_CacheGetValue(entry)) == NULL) {
	        if (Ns_CacheTimedWait(sobPtr->cache, &timeout) != NS_OK) {
		    break;
		}
	    	entry = Ns_CacheFindEntry(sobPtr->cache, key);
	    }
	}

    } else {

    	/*
	 * Cache miss:  Send request to read file from server and
	 * either copy the result or parse into board format.
	 */

	cached = 0;
    	Ns_CacheUnlock(sobPtr->cache);
	Ns_DStringAppend(&in, key);
        len = 0; /* to quiet the compiler */
	if ((n = Dci_RpcSend(sobPtr->rpc, DCI_NFSCMDREAD, &in, &out)) == RPC_OK) {
	    len = out.length;
	    if (cbPtr == NULL) {
	    	data = Ns_DStringExport(&out);
	    } else  {
		data = (char *) Dci_CbParse(out.string);
	    }
	} else {
            Dci_RpcTclError(interp, n);
        }
	Ns_CacheLock(sobPtr->cache);

    	/*
	 * If the fetch suceeded, update the value. Otherwise, flush
	 * the entry.
	 */

	if (data != NULL) {
	    entry = Ns_CacheCreateEntry(sobPtr->cache, key, &new);
	    Ns_CacheSetValueSz(entry, data, (size_t)len);
	} else {
	    entry = Ns_CacheFindEntry(sobPtr->cache, key);
	    if (entry != NULL) {
	    	Ns_CacheFlushEntry(entry);
	    }
	}
	Ns_CacheBroadcast(sobPtr->cache);
    }

    /*
     * If data was fetched and/or located in cache, transfer to
     * this thread.
     */

    if (data != NULL) {
	if (copyPtr != NULL) {
	    
	    /*
	     * For NRS data file fetch, copy data to given pointer
	     * which is then cached in per-thread, per-connection
	     * table.
	     */
	     
	    *copyPtr = ns_strdup(data);
	    
	} else if (cbPtr == NULL) {
	
	    /*
	     * For ordinary SOB fetch, copy to interp.
	     */
	     
    	    Tcl_SetResult(interp, data, TCL_VOLATILE);
	    
    	} else {
	    Dci_Board *board;
	    char *msgs;

	    /*
	     * For boards, extract the requested messages.
	     */

	    board = (Dci_Board *) data;
	    msgs = Dci_CbGet(board, cbPtr->first, cbPtr->last, &cbPtr->npost);
	    Tcl_SetResult(interp, msgs, (Tcl_FreeProc *) ns_free);
	
	}
    }
    Ns_CacheUnlock(sobPtr->cache);

done:
    if (fDebug) {
	Ns_Log(data ? Notice : Warning, "%s: get %s - %s%s",
	    sobPtr->name, key,
	    data ? "ok" : "failed", cached ? " (cached)": "");
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    
    /*
     * Leave an error message in the interp and return error
     * unless this is an NRS data file fetch.
     */

    if (data == NULL && copyPtr == NULL) {
    	Tcl_AppendResult(interp, "could not fetch: ", key, NULL);
	return TCL_ERROR;
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NcbFreeBoard --
 *
 *      This is the Dci_SobCreate() freeProc callback for comment boards.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Free memory associated with a comment board.
 *
 *----------------------------------------------------------------------
 */
 
static void
NcbFreeBoard(void *arg)
{
    Dci_Board *board = (Dci_Board *) arg;

    Dci_CbFree(board);
}


/*
 *----------------------------------------------------------------------
 *
 * Nrs_SobCreate, Nrs_SobGetFile --
 *
 *      Support routines for legacy system not part of the
 *	nsdci module.
 *
 * Results:
 *      As needed for NRS.
 *
 * Side effects:
 *      As needed for NRS.
 *
 *----------------------------------------------------------------------
 */
 
static Sob *nrsSobPtr;

int
DciNrsCreate(char *server, char *module, char *name, char *path)
{
    int nocache;

    if (Ns_ConfigGetBool(path, "nocache", &nocache) && nocache) {
	Ns_Log(Error, "nrs: sob must support caching");
	return NS_ERROR;
    }
    nrsSobPtr = SobCreate(server, module, name, path, 0);
    if (nrsSobPtr == NULL) {
	return NS_ERROR;
    }
    return NS_OK;
}

char *
DciNrsGet(Tcl_Interp *interp, void *arg, char *key, char **copyPtr)
{
    char *data = NULL;

    if (nrsSobPtr != NULL) {
	(void) SobGetFile(interp, nrsSobPtr, key, &data, NULL);
    }
    return data;
}
