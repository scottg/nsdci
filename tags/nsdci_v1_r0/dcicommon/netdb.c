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

#define NDB_GET 1
#define NDB_SET 2
#define NDB_DELETE 3

typedef struct NdbServer {
    Ns_Mutex    lock;
    Dci_Rpc     *rpc;
    GDBM_FILE   db;
    char        *dbfile;
} NdbServer;

static int fDebug;
static Ns_TclInterpInitProc AddClientCmds;

static int GetServer(Tcl_Interp *interp, char *name, NdbServer **sPtrPtr);
static int GetClient(Tcl_Interp *interp, char *name, Dci_Rpc **rpcPtr);
static int GetKey(Tcl_Interp *interp, char *arg, char **keyPtr);
static char *DbsGet(NdbServer *ndbsPtr, char *);
static int DbsSet(NdbServer *ndbsPtr, char *, char *);
static int DbsDelete(NdbServer *ndbsPtr, char *);
static Dci_RpcProc DbsProc;
static Ns_Callback DbsClose;
static Tcl_CmdProc Dbc2Cmd;
static Tcl_CmdProc DbcGetCmd;
static Tcl_CmdProc DbcDeleteCmd;
static Tcl_CmdProc DbcSetCmd;
static Tcl_CmdProc DbsDeleteCmd;
static Tcl_CmdProc DbcDeleteCmd;
static Tcl_CmdProc DbsSetCmd;
static Tcl_CmdProc DbsGetCmd;
static Tcl_CmdProc DbsDatabaseCmd;
static Tcl_CmdProc DbsBackupCmd;
static Tcl_CmdProc DbsSyncCmd;
static Ns_TclInterpInitProc AddServerCmds;
static int Send(Tcl_Interp *interp, Dci_Rpc *rpc, int cmd, char *key, char *value);

static Tcl_HashTable serverTable;
static Tcl_HashTable clientTable;
static Dci_Rpc *ndbRpc;

int
DciNetdbInit(char *server, char *module)
{
    Ns_Set *set, *cliSet;
    Ns_DString ds;
    char *path, *name;
    char rpcname[DCI_RPCNAMESIZE];
    int i, new, timeout;
    Tcl_HashEntry *hPtr;
    Dci_Rpc *rpc;
    NdbServer *ndbsPtr;
    
    Dci_LogIdent(module, rcsid);
    
    Ns_DStringInit(&ds);
    Ns_ModulePath(&ds, server, module, "ndb", NULL);
    if (mkdir(ds.string, 0755) != 0 && errno != EEXIST) {
	Ns_Log(Error, "ndb: mkdir(%s) failed: %s", 
	    ds.string, strerror(errno));
	return NS_ERROR;
    }
    Ns_DStringFree(&ds);
                
    Tcl_InitHashTable(&serverTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&clientTable, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "ndb", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
    	fDebug = 0;
    }

    /*
     * Create netdb clients.
     */
    
    path = Ns_ConfigGetPath(server, module, "ndb/clients", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
    	name = Ns_SetKey(set, i);
	if (Dci_RpcName("ndb", name, rpcname) != NS_OK) {
	    return NS_ERROR;
	}
        path = Ns_ConfigGetPath(server, module, "ndb/client", name, NULL);
        hPtr = Tcl_CreateHashEntry(&clientTable, name, &new);
        if (!new) {
            Ns_Log(Error, "ndb(%s) already exists", name);
            return NS_ERROR;
        }
	if (!Ns_ConfigGetInt(path, "timeout", &timeout) || timeout < 1) {
    	    timeout = 2;
	}
        rpc = Dci_RpcCreateClient(server, module, rpcname, timeout);
	if (rpc == NULL) {
	    return NS_ERROR;
	}
        Tcl_SetHashValue(hPtr, rpc);
        Ns_TclInitInterps(server, AddClientCmds, NULL);
    }
    
    /*
     * Create the default netdb client.
     */
    
    path = Ns_ConfigGetPath(server, module, "ndb/client", NULL);
    if (path != NULL) {
        if (!Ns_ConfigGetInt(path, "timeout", &timeout) || timeout < 1) {
            timeout = 2;
        }
        ndbRpc = Dci_RpcCreateClient(server, module, "ndb:ndb", timeout);
        if (ndbRpc == NULL) {
            return NS_ERROR;
        }
        Ns_TclInitInterps(server, AddClientCmds, NULL);
    }
        
    /*
     * Create netdb servers.
     */
    
    path = Ns_ConfigGetPath(server, module, "ndb/servers", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
        name = Ns_SetKey(set, i);
	if (Dci_RpcName("ndb", name, rpcname) != NS_OK) {
	    return NS_ERROR;
	}
        path = Ns_ConfigGetPath(server, module, "ndb/server", name, NULL);
        hPtr = Tcl_CreateHashEntry(&serverTable, name, &new);
        if (!new) {
            Ns_Log(Error, "ndb(%s) already exists", name);
            return NS_ERROR;
        }
        ndbsPtr = ns_calloc(1, sizeof(*ndbsPtr));
        Ns_MutexSetName2(&ndbsPtr->lock, "ndb", name);
        ndbsPtr->dbfile = Ns_ConfigGetValue(path, "database");
	if (ndbsPtr->dbfile == NULL) {
	    Ns_Log(Error, "ndb: missing database for ndb server: %s", name);
	    return NS_ERROR;
	}
	ndbsPtr->db = Dci_GdbmOpen(ndbsPtr->dbfile);
	if (ndbsPtr->db == NULL) {
	    Ns_Log(Error, "ndbs: gdbm_open(%s) failed: %s", 
	    	ndbsPtr->dbfile, gdbm_strerror(gdbm_errno));
	    return NS_ERROR;
	}
        Tcl_SetHashValue(hPtr, ndbsPtr);
        Ns_Log(Notice, "ndb: opened: %s", ndbsPtr->dbfile);
	Ns_RegisterAtExit(DbsClose, ndbsPtr);
        path = Ns_ConfigGetPath(server, module, "ndb/server", name, "clients", NULL);
        cliSet = Ns_ConfigGetSection(path);
    	if (Dci_RpcCreateServer(server, module, rpcname, NULL, cliSet, DbsProc,
				ndbsPtr) != NS_OK) {
	    return NS_ERROR;
        }
    }
    if (set != NULL) {
        Ns_TclInitInterps(server, AddServerCmds, NULL);
    }
    return NS_OK;
}


static int
AddClientCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "ndbc.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "ndbc2.get", Dbc2Cmd, DbcGetCmd, NULL);
    Tcl_CreateCommand(interp, "ndbc2.delete", Dbc2Cmd, DbcDeleteCmd, NULL);
    Tcl_CreateCommand(interp, "ndbc2.set", Dbc2Cmd, DbcSetCmd, NULL);
    
    if (ndbRpc != NULL) {
        Tcl_CreateCommand(interp, "ndbc.get", DbcGetCmd, ndbRpc, NULL);
        Tcl_CreateCommand(interp, "ndbc.delete", DbcDeleteCmd, ndbRpc, NULL);
        Tcl_CreateCommand(interp, "ndbc.set", DbcSetCmd, ndbRpc, NULL);
    }
    
    return NS_OK;
}


static int
AddServerCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "ndbs.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "ndbs.delete", DbsDeleteCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ndbs.get", DbsGetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ndbs.set", DbsSetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ndbs.backup", DbsBackupCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ndbs.database", DbsDatabaseCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ndbs.sync", DbsSyncCmd, NULL, NULL);
    return TCL_OK;
}

static int
GetServer(Tcl_Interp *interp, char *name, NdbServer **sPtrPtr)
{
    Tcl_HashEntry *hPtr;
    
    hPtr = Tcl_FindHashEntry(&serverTable, name);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp, "no such ndb server: ", name, NULL);
	return TCL_ERROR;
    }
    *sPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


static int
GetClient(Tcl_Interp *interp, char *name, Dci_Rpc **rpcPtr)
{
    Tcl_HashEntry *hPtr;
    
    hPtr = Tcl_FindHashEntry(&clientTable, name);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp, "no such ndb client: ", name, NULL);
	return TCL_ERROR;
    }
    *rpcPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


static int
GetKey(Tcl_Interp *interp, char *arg, char **keyPtr)
{
    if (arg[0] == '\0') {
        Tcl_AppendResult(interp, "attempt to pass null key", NULL);
        return TCL_ERROR;
    }
    *keyPtr = arg;
    return TCL_OK;
}


static int
DbcGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc = (Dci_Rpc *) arg;
    char *key;
    
    if (argc != 2) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " key\"", NULL);
	return TCL_ERROR;
    }
    if (GetKey(interp, argv[argc-1], &key) != TCL_OK) {
	return TCL_ERROR;
    }
    return Send(interp, rpc, NDB_GET, key, NULL);
}

static int
DbcDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc = (Dci_Rpc *) arg;
    char *key;
    
    if (argc != 2) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " key\"", NULL);
	return TCL_ERROR;
    }
    if (GetKey(interp, argv[argc-1], &key) != TCL_OK) {
	return TCL_ERROR;
    }
    return Send(interp, rpc, NDB_DELETE, key, NULL);
}

static int
DbcSetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc = (Dci_Rpc *) arg;
    char *key, *value;
    
    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " key value\"", NULL);
	return TCL_ERROR;
    }
    if (GetKey(interp, argv[argc-2], &key) != TCL_OK) {
	return TCL_ERROR;
    }
    value = argv[argc-1];
    return Send(interp, rpc, NDB_SET, key, value);
}

static int
Dbc2Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_CmdProc *proc = arg;
    Dci_Rpc *rpc;
    char *nargv[7];
    int i, nargc;
    
    if (argc < 2 || argc > 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb ?...?\"", NULL);
	return TCL_ERROR;
    }
    if (GetClient(interp, argv[1], &rpc) != TCL_OK) {
        return TCL_ERROR;
    }
    nargc = 0;
    nargv[nargc++] = argv[0];
    for (i = 2; i < argc; ++i) {
	nargv[nargc++] = argv[i];
    }
    nargv[nargc] = NULL;
    return (*proc)(rpc, interp, nargc, nargv);
}

static int
Send(Tcl_Interp *interp, Dci_Rpc *rpc, int cmd, char *key, char *value)
{
    Ns_DString in, out;
    int result, n;

    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    Ns_DStringAppendElement(&in, key);
    if (value != NULL) {
    	Ns_DStringAppendElement(&in, value);
    }
    if ((n = Dci_RpcSend(rpc, cmd, &in, &out)) != RPC_OK) {
    	Tcl_AppendResult(interp, "could not send ndb request: ", 
            Dci_RpcTclError(interp, n), NULL);
	result = TCL_ERROR;
    } else {
    	Tcl_SetResult(interp, out.string, TCL_VOLATILE);
	result = TCL_OK;
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return result;
}


static int
DbsProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    char **largv, *value;
    int largc, status, err;
    NdbServer *ndbsPtr = (NdbServer *) arg;
    
    if (Tcl_SplitList(NULL, inPtr->string, &largc, &largv) != TCL_OK) {
	return NS_ERROR;
    }
    status = NS_ERROR;
    if (cmd == NDB_GET) {
	if (largc != 1) {
	    goto done;
	}
	value = DbsGet(ndbsPtr, largv[0]);
	if (value != NULL) {
	    Ns_DStringAppend(outPtr, value);
            gdbm_free(value);
	}
    } else {
	if (cmd == NDB_DELETE) {
	    if (largc != 1) {
		goto done;
	    }
	    err = DbsDelete(ndbsPtr, largv[0]);
	} else if (cmd == NDB_SET) {
	    if (largc != 2) {
		goto done;
	    }
	    err = DbsSet(ndbsPtr, largv[0], largv[1]);
	} else {
	    goto done;
	}
	Ns_DStringAppend(outPtr, err ? "-1" : "0");
    }
    status = NS_OK;
done:
    ckfree((char *) largv);
    return status;
}


static void
DbsClose(void *arg)
{
    NdbServer *ndbsPtr = (NdbServer *) arg;
    
    Ns_Log(Notice, "ndbs: closing: %s", ndbsPtr->dbfile);
    Ns_MutexLock(&(ndbsPtr->lock));
    gdbm_sync(ndbsPtr->db);
    gdbm_close(ndbsPtr->db);
    Ns_MutexUnlock(&(ndbsPtr->lock));
}


static void
DbsDatum(datum *keyPtr, char *string)
{
    keyPtr->dptr = string;
    keyPtr->dsize = strlen(string)+1;
}


static int
DbsSet(NdbServer *ndbsPtr, char *key, char *value)
{
    datum dkey, dvalue;
    int result;

    if (key == NULL) {
        return 1;
    }
    DbsDatum(&dkey, key);
    DbsDatum(&dvalue, value);
    Ns_MutexLock(&(ndbsPtr->lock));
    result = gdbm_store(ndbsPtr->db, dkey, dvalue, GDBM_REPLACE);
    Ns_MutexUnlock(&(ndbsPtr->lock));
    
    return result;
}


static char *
DbsGet(NdbServer *ndbsPtr, char *key)
{
    datum dkey, dvalue;
    
    if (key == NULL) {
        return NULL;
    }
    DbsDatum(&dkey, key);
    Ns_MutexLock(&(ndbsPtr->lock));
    dvalue = gdbm_fetch(ndbsPtr->db, dkey);
    Ns_MutexUnlock(&(ndbsPtr->lock));
    
    return dvalue.dptr;
}


static int
DbsDelete(NdbServer *ndbsPtr, char *key)
{
    datum dkey;
    int result;
    
    if (key == NULL) {
        return 1;
    }
    DbsDatum(&dkey, key);
    Ns_MutexLock(&(ndbsPtr->lock));
    result = gdbm_delete(ndbsPtr->db, dkey);
    Ns_MutexUnlock(&(ndbsPtr->lock));
    
    return result;
}


static int
DbsDeleteCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    NdbServer *ndbsPtr;
    
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb key\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Dci_SetIntResult(interp, DbsDelete(ndbsPtr, argv[2]));
    return TCL_OK;
}


static int
DbsGetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *value, *result;
    NdbServer *ndbsPtr;
    
    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb key ?varName?\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    value = DbsGet(ndbsPtr, argv[2]);
    if (argc == 3) {
	if (value == NULL) {
	    Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
	    return TCL_ERROR;
	}
	Tcl_SetResult(interp, value, (Tcl_FreeProc *) gdbm_free);
    } else {
	if (value == NULL) {
	    Tcl_SetResult(interp, "0", TCL_STATIC);
	} else {
	    result = Tcl_SetVar(interp, argv[3], value, TCL_LEAVE_ERR_MSG);
	    gdbm_free(value);
	    if (result == NULL) {
	    	return TCL_ERROR;
	    }
	    Tcl_SetResult(interp, "1", TCL_STATIC);
	}
    }
    return TCL_OK;
}
	

static int
DbsSetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    NdbServer *ndbsPtr;
    
    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " key ?value?\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc == 3) {
	return DbsGetCmd(dummy, interp, argc, argv);
    }
    if (DbsSet(ndbsPtr, argv[2], argv[3]) != 0) {
	Tcl_AppendResult(interp, "could not set \"", argv[2],
	    "\": ", gdbm_strerror(gdbm_errno), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
DbsDatabaseCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    NdbServer *ndbsPtr;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, ndbsPtr->dbfile, TCL_STATIC);
    return TCL_OK;
}

static int
DbsSyncCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    NdbServer *ndbsPtr;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    gdbm_sync(ndbsPtr->db);
    return TCL_OK;
}

static int
DbsBackupCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
#define BUFSIZE 8192
    char buf[BUFSIZE];
    int n, code, in, out;
    NdbServer *ndbsPtr;
    
    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " netdb backupFile ?maxroll?\"", NULL);
	return TCL_ERROR;
    }
    if (GetServer(interp, argv[1], &ndbsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc == 4) {
	if (Tcl_GetInt(interp, argv[3], &n) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (Ns_RollFile(argv[2], n) != NS_OK) {
	    Tcl_AppendResult(interp, "could not roll \"", argv[2],
		"\"", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}
    }
    in = open(ndbsPtr->dbfile, O_RDONLY);
    if (in <= 0) {
	Tcl_AppendResult(interp, "could not open \"", ndbsPtr->dbfile,
	    "\" for read: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    out = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (out <= 0) {
	close(in);
	Tcl_AppendResult(interp, "could not open \"", argv[2],
	    "\" for write: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }

    code = TCL_ERROR;
    Ns_MutexLock(&(ndbsPtr->lock));
    gdbm_sync(ndbsPtr->db);
    while ((n = read(in, buf, BUFSIZE)) > 0) {
	if (write(out, buf, (size_t)n) != n) {
	    Tcl_AppendResult(interp, "write to \"", argv[2],
		"\" failed: ", Tcl_PosixError(interp), NULL);
	    break;
	}
    }
    Ns_MutexUnlock(&(ndbsPtr->lock));
    if (n == 0) {
	code = TCL_OK;
    } else if (n < 0) {
	Tcl_AppendResult(interp, "read from \"", ndbsPtr->dbfile,
	    "\" failed: ", Tcl_PosixError(interp), NULL);
    }
    close(in);
    close(out);
    return code;
}
