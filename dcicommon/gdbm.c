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

static Ns_TclInterpInitProc AddCmds;

static Tcl_CmdProc
	GdbmOpenCmd, GdbmCloseCmd, 
	GdbmGetCmd, GdbmFirstCmd, GdbmNextCmd, GdbmSetCmd,
	GdbmExistsCmd, GdbmDeleteCmd;

/*
 * The following maintains an open database.
 */

typedef struct {
    Ns_Mutex	lock;
    GDBM_FILE	file;
    Tcl_HashTable search;
} Db;

/*
 * The following maintains the per-server table of Db
 * structures, allocated to the configured max length.
 */

typedef struct {
    int	    max;
    Db	    dbs[1];
} Table;


int
DciGdbmInit(char *server, char *module)
{
    Table *tablePtr;
    Db *dbPtr;
    char *path;
    int max, i;

    Dci_LogIdent(module, rcsid);
    path = Ns_ConfigGetPath(server, module, "gdbm", NULL);
    if (!Ns_ConfigGetInt(path, "maxdbs", &max)) {
	max = 20;
    }
    tablePtr = ns_calloc(1, sizeof(Table) + sizeof(Db) * max);
    tablePtr->max = max;
    for (i = 0; i < max; ++i) {
	dbPtr = &tablePtr->dbs[i];
	Tcl_InitHashTable(&dbPtr->search, TCL_STRING_KEYS);
    }
    Ns_TclInitInterps(server, AddCmds, tablePtr);
    return NS_OK;
}


static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "gdbm.delete", GdbmDeleteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.get", GdbmGetCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.start", GdbmFirstCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.next", GdbmNextCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.set", GdbmSetCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.exists", GdbmExistsCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.open", GdbmOpenCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.close", GdbmCloseCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.getNextSearchResult", GdbmNextCmd, arg, NULL);
    Tcl_CreateCommand(interp, "gdbm.startSearch", GdbmFirstCmd, arg, NULL);
    return TCL_OK;
}


static int
GdbmLock(Table *tablePtr, Tcl_Interp *interp, char *id, Db **dbPtrPtr)
{
    Db *dbPtr;
    int idx;

    if (id[0] != 'g' || id[1] != 'd' ||
	Tcl_GetInt(NULL, id+2, &idx) != TCL_OK ||
	idx < 0 || idx >= tablePtr->max) {
	Tcl_AppendResult(interp, "invalid id: ", id, NULL);
	return TCL_ERROR;
    }
    dbPtr = &tablePtr->dbs[idx];
    Ns_MutexLock(&dbPtr->lock);
    if (dbPtr->file != NULL) {
	*dbPtrPtr = dbPtr;
	return TCL_OK;
    }
    Ns_MutexUnlock(&dbPtr->lock);
    Tcl_AppendResult(interp, "no such open dbPtr: ", id, NULL);
    return TCL_ERROR;
}


static void
GdbmUnlock(Db *dbPtr)
{
    Ns_MutexUnlock(&dbPtr->lock);
}


static void
GdbmDatum(datum *keyPtr, char *string)
{
    keyPtr->dptr = string;
    keyPtr->dsize = strlen(string)+1;
}


static int
GdbmDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    datum key;
    int err;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr key\"", NULL);
	return TCL_ERROR;
    }
    GdbmDatum(&key, argv[2]);
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    err = gdbm_delete(dbPtr->file, key);
    GdbmUnlock(dbPtr);
    if (err != 0) {
    	Tcl_AppendResult(interp, "could not delete \"",
	    argv[2], "\"", gdbm_strerror(gdbm_errno), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
GdbmGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    datum key, value;
    char *result;

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr key ?varName?\"", NULL);
	return TCL_ERROR;
    }
    GdbmDatum(&key, argv[2]);
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    value = gdbm_fetch(dbPtr->file, key);
    GdbmUnlock(dbPtr);
    if (argc == 3) {
	if (value.dptr == NULL) {
	    Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
	    return TCL_ERROR;
	}
	Tcl_SetResult(interp, value.dptr, (Tcl_FreeProc *) gdbm_free);
    } else {
	if (value.dptr == NULL) {
	    Tcl_SetResult(interp, "0", TCL_STATIC);
	} else {
	    result = Tcl_SetVar(interp, argv[3], value.dptr, TCL_LEAVE_ERR_MSG);
	    gdbm_free(value.dptr);
	    if (result == NULL) {
	    	return TCL_ERROR;
	    }
	    Tcl_SetResult(interp, "1", TCL_STATIC);
	}
    }
    return TCL_OK;
}
	

static int
GdbmExistsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    datum key;
    int exists;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr key\"", NULL);
	return TCL_ERROR;
    }
    GdbmDatum(&key, argv[2]);
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    exists = gdbm_exists(dbPtr->file, key);
    GdbmUnlock(dbPtr);
    Tcl_SetResult(interp, exists ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


static int
GdbmSetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    datum key, value;
    int err;

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr key ?value?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3) {
	return GdbmGetCmd(arg, interp, argc, argv);
    }
    GdbmDatum(&key, argv[2]);
    GdbmDatum(&value, argv[3]);
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    err = gdbm_store(dbPtr->file, key, value, GDBM_REPLACE);
    GdbmUnlock(dbPtr);
    if (err != 0) {
	Tcl_AppendResult(interp, "could not set \"", argv[2],
	    "\": ", gdbm_strerror(gdbm_errno), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
GdbmCloseCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    datum *keyPtr;
    Db *dbPtr;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr\"", NULL);
	return TCL_ERROR;
    }
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    hPtr = Tcl_FirstHashEntry(&dbPtr->search, &search);
    while (hPtr != NULL) {
	keyPtr = Tcl_GetHashValue(hPtr);
	if (keyPtr->dptr != NULL) {
	    gdbm_free(keyPtr->dptr);
	}
	ns_free(keyPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&dbPtr->search);
    Tcl_InitHashTable(&dbPtr->search, TCL_STRING_KEYS);
    gdbm_close(dbPtr->file);
    dbPtr->file = NULL;
    GdbmUnlock(dbPtr);
    return TCL_OK;
}


static int
GdbmOpenCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Table *tablePtr = arg;
    Db *dbPtr = NULL; /* to quiet compiler */
    int i, idx;
    char buf[20];
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }
    idx = -1;
    for (i = 0; i < tablePtr->max; ++i) {
	dbPtr = &tablePtr->dbs[i];
	Ns_MutexLock(&dbPtr->lock);
	if (dbPtr->file == NULL) {
	    idx = i;
	    break;
	}
	Ns_MutexUnlock(&dbPtr->lock);
    }
    if (idx < 0) {
	Tcl_SetResult(interp, "too many open dbs", TCL_STATIC);
	return TCL_ERROR;
    }
    dbPtr->file = Dci_GdbmOpen(argv[1]);
    if (dbPtr->file == NULL) {
	idx = -1;
    }
    Ns_MutexUnlock(&dbPtr->lock);
    if (idx < 0) {
	Tcl_AppendResult(interp, "could not open \"", argv[1],
	    "\": ", gdbm_strerror(gdbm_errno), NULL);
	return TCL_ERROR;
    }
    sprintf(buf, "gd%d", idx);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
GdbmFirstCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    datum *keyPtr;
    Tcl_HashEntry *hPtr;
    int new, n;
    char buf[20];

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr\"", NULL);
	return TCL_ERROR;
    }
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    keyPtr = ns_malloc(sizeof(datum));
    *keyPtr = gdbm_firstkey(dbPtr->file);
    n = dbPtr->search.numEntries;
    do {
	sprintf(buf, "gs%d", n++);;
	hPtr = Tcl_CreateHashEntry(&dbPtr->search, buf, &new);
    } while (!new);
    Tcl_SetHashValue(hPtr, keyPtr);
    GdbmUnlock(dbPtr);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
GdbmNextCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Db *dbPtr;
    Tcl_HashEntry *hPtr;
    datum *keyPtr;
    int result;

    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " dbPtr search keyVar\"", NULL);
	return TCL_ERROR;
    }
    if (GdbmLock(arg, interp, argv[1], &dbPtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    hPtr = Tcl_FindHashEntry(&dbPtr->search, argv[2]);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such search: ", argv[2], NULL);
	result = TCL_ERROR;
    } else {
	result = TCL_OK;
	keyPtr = Tcl_GetHashValue(hPtr);
	if (keyPtr->dptr == NULL) {
	    Tcl_DeleteHashEntry(hPtr);
	    ns_free(keyPtr);
	    Tcl_SetResult(interp, "0", TCL_STATIC);
	} else {
	    if (Tcl_SetVar(interp, argv[3], keyPtr->dptr, TCL_LEAVE_ERR_MSG) == NULL) {
		result = TCL_ERROR;
	    } else {
		*keyPtr = gdbm_nextkey(dbPtr->file, *keyPtr);
		Tcl_SetResult(interp, "1", TCL_STATIC);
	    }
	}
    }
    GdbmUnlock(dbPtr);
    return result;
}


GDBM_FILE
Dci_GdbmOpen(char *path)
{
    GDBM_FILE file;

    file = gdbm_open(path, 0, GDBM_WRCREAT | GDBM_FAST, 0644, NULL);
    if (file != NULL) {
        Ns_CloseOnExec(gdbm_fdesc(file));
    }
    return file;
}
