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
#include <sys/mman.h>

static char rcsid[] = "$Id$";

/*
 * The following structure defines a loaded Av.
 */

typedef struct Av {
    char       *name;		/* Av name. */
    uint32_t	id;		/* Unique in-memory id. */
    int		refcnt;		/* Ref count of all attached interps. */
    struct stat st;		/* Last known stat info for av. */
    Dci_List	meta;		/* Av meta data section. */
    Dci_List	data;		/* Av data section. */
} Av;

/*
 * The following structure maintains per-interp Av.
 */

typedef struct AvData {
    Tcl_HashTable avTable;	/* Table of current Av's. */
    Tcl_Command  ucmd;		/* Token and unknown command. */
    Tcl_CmdInfo  uinfo;		/* Info for previous unknown command. */
    char	*config;	/* Config Av. */
    char	*autoname;	/* Name for auto-load av. */
    uint32_t	 autoid;	/* Unique id of auto-load av. */
    int		 nprocs;	/* Number of auto-loaded procs. */
    Tcl_DString  procs;		/* Array of currently auto-loaded procs. */
} AvData;

/*
 * The following structure is used to wrap the Tcl proc delete for Av-based
 * auto-loaded procs.
 */

typedef struct AvProc {
    AvData	*dataPtr;
    int		 pidx;
    Tcl_Command  cmd;
    Tcl_CmdDeleteProc *deleteProc;
    ClientData deleteData;
} AvProc;

static Ns_Cache *AvCache(void);
static Ns_Callback AvFree;
static void AvRelease(AvData *dataPtr);
static int AvGet(ClientData arg, Tcl_Interp *interp, char *list, Av **avPtrPtr);
static void AvCleanup(Tcl_Interp *interp, AvData *dataPtr);
static Ns_TclTraceProc AvAtCleanup;
static Ns_TclInterpInitProc AvConfigInit;
static Tcl_InterpDeleteProc AvAssocDelete;
static char *AvPath(Ns_DString *dsPtr, char *file);
static void AppendHdr(Tcl_DString *dsPtr, char *file, Dci_List *metaPtr,
		      Dci_List *dataPtr);
static void AppendMeta(Tcl_DString *dsPtr, char *str, int max);
static void AppendLengths(Tcl_DString *dsPtr, Dci_List *listPtr, int *offPtr,
			  int *lenPtr);
static void AppendStrings(Tcl_DString *dsPtr, Dci_List *listPtr, int off,
			  int len);
static int AvGetAuto(AvData *dataPtr, Tcl_Interp *interp, Av **avPtrPtr);

static Tcl_CmdDeleteProc AvProcDelete;
static Tcl_ObjCmdProc UnknownObjCmd;

static Tcl_CmdProc AvCleanupCmd, AvConfigCmd, AvCreateCmd, AvDumpCmd,
	AvFileCmd, AvUnknownCmd;
static Tcl_CmdProc AvDumpCmd, AvFindCmd, AvGetCmd, AvKeyCmd,
	AvKeysCmd, AvLengthCmd, AvValueCmd;
static Tcl_CmdProc AvMDumpCmd, AvMFindCmd, AvMGetCmd, AvMKeyCmd,
	AvMKeysCmd, AvMLengthCmd, AvMValueCmd;

typedef int (AvSubCmd)(ClientData arg, Tcl_Interp *interp, int argc,
		       char **argv, int cmd);

static AvSubCmd AvDumpSubCmd, AvLengthSubCmd, AvFindSubCmd, AvGetSubCmd;

/*
 * The following config variables can be set via the module init.
 */

static char *configonce = "av.unknown [av.config]; config.once";
static char *assoc = "dci:av";
static char *moddir = NULL;
static int maxlists = 10;
static int debug;


/*
 *----------------------------------------------------------------------
 *
 * DciAvLibInit --
 *
 *      Library entry point.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Adds ident string.
 *
 *----------------------------------------------------------------------
 */
void
DciAvLibInit(void)
{
    DciAddIdent(rcsid);
}


/*
 *----------------------------------------------------------------------
 *
 * DciAvTclInit --
 *
 *      Adds the av commands.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

int
DciAvTclInit(Tcl_Interp *interp)
{
    AvData *dataPtr;

    /* NB: Final cleanup handled by AvAssocDelete. */
    dataPtr = ns_calloc(1, sizeof(AvData));
    Tcl_DStringInit(&dataPtr->procs);
    Tcl_InitHashTable(&dataPtr->avTable, TCL_STRING_KEYS);
    Tcl_SetAssocData(interp, assoc, AvAssocDelete, dataPtr);  

    /*
     * Management commands.
     */

    Tcl_CreateCommand(interp, "av.cleanup", AvCleanupCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.config", AvConfigCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.file", AvFileCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.create", AvCreateCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.debug", DciSetDebugCmd, &debug, NULL);
    Tcl_CreateCommand(interp, "av.unknown", AvUnknownCmd, dataPtr, NULL);

    /*
     * Data section commands.
     */

    Tcl_CreateCommand(interp, "av.dump", AvDumpCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.find", AvFindCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.get", AvGetCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.key", AvKeyCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.keys", AvKeysCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.length", AvLengthCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.value", AvValueCmd, dataPtr, NULL);

    /*
     * Meta data section commands.
     */

    Tcl_CreateCommand(interp, "av.mdump", AvMDumpCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mfind", AvMFindCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mget", AvMGetCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mkey", AvMKeyCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mkeys", AvMKeysCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mlength", AvMLengthCmd, dataPtr, NULL);
    Tcl_CreateCommand(interp, "av.mvalue", AvMValueCmd, dataPtr, NULL);
    /* NB: Previous name from av.mget. */
    Tcl_CreateCommand(interp, "av.meta", AvMGetCmd, dataPtr, NULL);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DciAvModInit --
 *
 *      Sets Av options based on aolserver config.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *  	May update the cache settings and sets and/or creates av directory.
 *
 *----------------------------------------------------------------------
 */

int
DciAvModInit(char *server, char *module)
{
    char *path, *config;
    Ns_DString ds;
    int max;
    path = Ns_ConfigPath(server, module, "av", NULL);
    if (Ns_ConfigGetInt(path, "maxlists", &max)) {
	maxlists = max;
    }
    moddir = Ns_ConfigGet(path, "dir");
    if (moddir == NULL) {
    	Ns_DStringInit(&ds);
    	Ns_ModulePath(&ds, server, module, "av", NULL);
	moddir = Ns_DStringExport(&ds);
    }
    if (mkdir(moddir, 0755) != 0 && errno != EEXIST) {
	Ns_Log(Error, "av: mkdir(%s) failed: %s", moddir, strerror(errno));
	return NS_ERROR;
    }
    config = Ns_ConfigGet(path, "config");
    if (config != NULL) {
	return Ns_TclInitInterps(server, AvConfigInit, config);
    }
    Ns_TclRegisterAtCleanup(AvAtCleanup, NULL);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvLengthCmd, AvMLengthCmd --
 *
 *      Implements av.length and av.mlength to return number of items
 *	in the data or meta data section.
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
AvLengthCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvLengthSubCmd(arg, interp, argc, argv, 'l');
}

static int
AvMLengthCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvLengthSubCmd(arg, interp, argc, argv, 'L');
}

static int
AvLengthSubCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv,
	     int cmd)
{
    Dci_List *listPtr;
    Av *avPtr;
    char buf[40];

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " av\"", NULL);
        return TCL_ERROR;
    }
    if (AvGet(arg, interp, argv[1], &avPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (cmd == 'L') {
	listPtr = &avPtr->meta;
    } else {
	listPtr = &avPtr->data;
    }
    sprintf(buf, "%d", (int) listPtr->nelem);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvDumpCmd, AvKeysCmd, AvMDumpCmd, AvMKeysCmd --
 *
 *      Implements av.dump, av.keys, av.mdump, and av.mkeys to return
 *	the keys or keys and values from the data or meta data section.
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
AvDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvDumpSubCmd(arg, interp, argc, argv, 'd');
}

static int
AvKeysCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvDumpSubCmd(arg, interp, argc, argv, 'k');
}

static int
AvMDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvDumpSubCmd(arg, interp, argc, argv, 'D');
}

static int
AvMKeysCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvDumpSubCmd(arg, interp, argc, argv, 'K');
}

static int
AvDumpSubCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int cmd)
{
    Dci_List *listPtr;
    Av *avPtr;
    int values;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " av ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (AvGet(arg, interp, argv[1], &avPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (cmd == 'D' || cmd == 'K') {
	listPtr = &avPtr->meta;
    } else {
	listPtr = &avPtr->data;
    }
    if (cmd == 'd' || cmd == 'D') {
	values = 1;
    } else {
	values = 0;
    }
    Dci_ListDump(interp, listPtr, argv[2], values);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvKeyCmd, AvValueCmd, AvMKeyCmd, AvMValueCmd --
 *
 *      Implements av.key, av.value, av.mkey, and av.mvalue commands to
 *	return key or value by index from the data or meta data sections.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	None
 *
 *----------------------------------------------------------------------
 */

static int
AvKeyCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvGetSubCmd(arg, interp, argc, argv, 'k');
}

static int
AvValueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvGetSubCmd(arg, interp, argc, argv, 'v');
}

static int
AvMKeyCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvGetSubCmd(arg, interp, argc, argv, 'K');
}

static int
AvMValueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvGetSubCmd(arg, interp, argc, argv, 'V');
}

static int
AvGetSubCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int cmd)
{
    Av *avPtr;
    Dci_List *listPtr;
    int idx;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " av index\"", NULL);
        return TCL_ERROR;
    }
    if (AvGet(arg, interp, argv[1], &avPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &idx) != TCL_OK) {
	return TCL_ERROR;
    } 
    if (cmd == 'K' || cmd == 'V') {
	listPtr = &avPtr->meta;
    } else {
	listPtr = &avPtr->data;
    }
    if (idx < 0 || idx >= listPtr->nelem) {
	Tcl_AppendResult(interp, "invalid index: ", argv[2], NULL);	
	return TCL_ERROR;
    }
    if (cmd == 'k' || cmd == 'K') {
        Tcl_SetResult(interp, Dci_ListKey(listPtr, idx), TCL_VOLATILE);
    } else {
        Tcl_SetResult(interp, Dci_ListValue(listPtr, idx), TCL_VOLATILE);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvFindCmd, AvGetCmd, AvMFindCmd, AvMGetCmd --
 *
 *      Implements the av.find, av.get, av.mfind, and av.mget commands
 *	to return the index or value for data or meta sections by string
 *	key.
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
AvFindCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvFindSubCmd(arg, interp, argc, argv, 'f');
}

static int
AvGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvFindSubCmd(arg, interp, argc, argv, 'g');
}

static int
AvMFindCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvFindSubCmd(arg, interp, argc, argv, 'F');
}

static int
AvMGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return AvFindSubCmd(arg, interp, argc, argv, 'G');
}

static int
AvFindSubCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int cmd)
{
    Av *avPtr;
    Dci_List *listPtr;
    Dci_Elem *elemPtr;
    char buf[40];
    int idx;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " av key\"", NULL);
        return TCL_ERROR;
    }
    if (AvGet(arg, interp, argv[1], &avPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (cmd == 'F' || cmd == 'G') {
	listPtr = &avPtr->meta;
    } else {
	listPtr = &avPtr->data;
    }
    elemPtr = Dci_ListSearch(listPtr, argv[2]);
    if (cmd == 'f' || cmd == 'F') {
	if (elemPtr == NULL) {
	    idx = -1;
	} else {
	    idx = elemPtr - listPtr->elems;
	}
	sprintf(buf, "%d", idx);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else if (elemPtr != NULL) {
        Tcl_SetResult(interp, elemPtr->value, TCL_VOLATILE);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvFileCmd --
 *
 *      Implements the av.file command to return the underlying filename
 *	for a given Av.
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
AvFileCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Tcl_SetResult(interp, AvPath(&ds, argv[1]), TCL_VOLATILE);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvCleanupCmd --
 *
 *      Implements the av.cleanup command.  This call is expected to
 *	be run periodically to release av's which may change between
 *	transactions.  See also the AvAtCleanup trace.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Will unload any loaded av's.
 *
 *----------------------------------------------------------------------
 */

static int
AvCleanupCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    AvCleanup(interp, arg);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvConfigCmd --
 *
 *      Return the config av, if any.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Will unload any loaded av's.
 *
 *----------------------------------------------------------------------
 */

static int
AvConfigCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    AvData *dataPtr = arg;

    Tcl_SetResult(interp, dataPtr->config, TCL_STATIC);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvCreateCmd --
 *
 *      Implements the av.create command to write out a new av file
 *	based on given data and optionally meta data section.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Will write cooresponding file to disk.
 *
 *----------------------------------------------------------------------
 */

static int
AvCreateCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_DString ds;
    int fd, moff, doff, mlen, dlen, result;
    Dci_List meta, data;
    char *metastr;
    
    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " file data ?meta?\"", NULL);
        return TCL_ERROR;
    }
    if (Dci_ListInit(&data, argv[2]) != TCL_OK) {
	Tcl_AppendResult(interp, "invalid list data: ", argv[2], NULL);
	return TCL_ERROR;
    }
    metastr = argv[3] ? argv[3] : "";
    if (Dci_ListInit(&meta, metastr) != TCL_OK) {
	Tcl_AppendResult(interp, "invalid meta data: ", metastr, NULL);
	Dci_ListFree(&data);
	return TCL_ERROR;
    }

    /*
     * Open the file.
     */

    result = TCL_ERROR;
    fd = open(argv[1], O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) {
    	Tcl_AppendResult(interp, "could not open \"", argv[1],
	    "\": ", Tcl_PosixError(interp), NULL);
	goto err;
    }

    /*
     * Append header, lengths, and strings.
     */

    AppendHdr(&ds, argv[1], &meta, &data);
    AppendLengths(&ds, &meta, &moff, &mlen);
    AppendLengths(&ds, &data, &doff, &dlen);
    AppendStrings(&ds, &meta, moff, mlen);
    AppendStrings(&ds, &data, doff, dlen);

    /*
     * Write out the dstring.
     */

    if (write(fd, ds.string, (size_t)ds.length) == ds.length) {
	result = TCL_OK;
    } else {
    	Tcl_AppendResult(interp, "could not write \"", argv[1],
	    "\": ", Tcl_PosixError(interp), NULL);
    }
    Tcl_DStringFree(&ds);
    close(fd);
err:
    Dci_ListFree(&meta);
    Dci_ListFree(&data);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * AvUnknownCommand --
 *
 *      Implements the av.unknown command to link a given av as the 
 *	source of auto-loaded procs.  This command should be called
 *	at regular intervals to catch possible changes to the av,
 *	e.g., within the ns_init proc.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *  	Will create the "unknown" command to create procs from the
 *	given av when needed.
 *
 *----------------------------------------------------------------------
 */

static int
AvUnknownCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    AvData *dataPtr = arg;
    Tcl_CmdInfo info;
    Tcl_Command cmd;
    
    if (argc > 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
			 argv[0], " ?av?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
	Tcl_SetResult(interp, dataPtr->autoname, TCL_VOLATILE);
	return TCL_OK;
    }
    if (dataPtr->autoname != NULL && STREQ(dataPtr->autoname, argv[1])) {
	return TCL_OK;
    }
    if (AvGetAuto(dataPtr, interp, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (dataPtr->autoname != NULL) {
    	ns_free(dataPtr->autoname);
    }
    dataPtr->autoname = ns_strdup(argv[1]);
    if (dataPtr->ucmd == NULL) {
	cmd = Tcl_FindCommand(interp, "unknown", NULL, TCL_GLOBAL_ONLY);
	if (cmd == NULL) {
	    cmd = Tcl_CreateObjCommand(interp, "unknown", UnknownObjCmd,
				       arg, NULL);
	}
	Tcl_GetCommandInfoFromToken(cmd, &dataPtr->uinfo);
	if (dataPtr->uinfo.objProc != UnknownObjCmd) {
	    info = dataPtr->uinfo;
	    info.objProc = UnknownObjCmd;
	    info.objClientData = arg;
	    Tcl_SetCommandInfoFromToken(cmd, &info);
	}
	dataPtr->ucmd = cmd;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UnknownObjCmd --
 *
 *      Implements the unkown command which will auto-load procs from
 *	previously requested av via av.unknown.  Tcl will redirect to
 *	this proc if it exists whenever a command is not defined.
 *
 * Results:
 *      Results of auto-loaded proc if successfully loaded, TCL_ERROR
 *	otherwise.
 *
 * Side effects:
 *  	May create a new proc.
 *
 *----------------------------------------------------------------------
 */

static int
UnknownObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    AvData *dataPtr = arg;
    Dci_Elem *elemPtr;
    Tcl_Command cmd;
    Tcl_CmdInfo info;
    char *proc, *msg;
    AvProc	*procPtr;
    Av		*avPtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "command ?args?");
	return TCL_ERROR;
    }
    if (AvGetAuto(dataPtr, interp, &avPtr) != TCL_OK) {
	msg = "could not get auto-load av";
	goto err;
    }
    if (avPtr == NULL) {
	msg = "auto-load av not enabled";
	goto err;
    }
    proc = Tcl_GetString(objv[1]);
    elemPtr = Dci_ListSearch(&avPtr->data, proc);
    if (elemPtr == NULL) {
	msg = "no proc script in auto-load av";
	goto err;
    }
    if (Tcl_Eval(interp, elemPtr->value) != TCL_OK) {
	msg = "invalid auto-load proc script";
	return TCL_ERROR;
    }
    cmd = Tcl_FindCommand(interp, elemPtr->key, NULL, 0);
    if (cmd == NULL) {
	msg = "script did not define proc";
	goto err;
    }

    /*
     * Wrap the proc's delete callback to ensure proper cleanup.
     */

    procPtr = ns_malloc(sizeof(AvProc));
    procPtr->dataPtr = dataPtr;
    procPtr->cmd = cmd;
    procPtr->pidx = dataPtr->nprocs++;
    Tcl_DStringAppend(&dataPtr->procs, (char *) &procPtr, sizeof(AvProc *));
    Tcl_GetCommandInfoFromToken(cmd, &info);
    procPtr->deleteProc = info.deleteProc;
    procPtr->deleteData = info.deleteData;
    info.deleteProc = AvProcDelete;
    info.deleteData = procPtr;
    Tcl_SetCommandInfoFromToken(cmd, &info);

    /*
     * Re-evaluate the command now that the proc is defined.
     */

    return Tcl_EvalObjv(interp, --objc, ++objv, 0);

err:
    Tcl_ResetResult(interp);
    if (dataPtr->uinfo.objProc != UnknownObjCmd) {
	return (*dataPtr->uinfo.objProc)(dataPtr->uinfo.objClientData,
					 interp, objc, objv);
    }
    Tcl_AppendResult(interp, "invalid command name \"", proc, "\" (",
		     msg, ")", NULL);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * AvConfigInit --
 *
 *      Tcl interp create callback to override default ns_init dummy
 *	proc with a one-time call to setup the av-based config.
 *
 * Results:
 *      NS_OK if evaluated, NS_ERROR otherwise.
 *
 * Side effects:
 *  	Will redefine ns_init proc.
 *
 *----------------------------------------------------------------------
 */

static int
AvConfigInit(Tcl_Interp *interp, void *arg)
{
    AvData *dataPtr;

    dataPtr = Tcl_GetAssocData(interp, assoc, NULL);
    dataPtr->config = arg;
    if (Tcl_EvalEx(interp, configonce, -1, 0) != TCL_OK) {
	Ns_TclLogError(interp);
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvGet --
 *
 *      Get an av by name, mapping it into memory if necesssary.
 *
 * Results:
 *      TCL_OK if av is availalble, TCL_ERROR otherwise.
 *
 * Side effects:
 *  	Will leave an error message in given interp if not sucessful.
 *
 *----------------------------------------------------------------------
 */

static int
AvGet(ClientData arg, Tcl_Interp *interp, char *list, Av **avPtrPtr)
{
    AvData *dataPtr = arg;
    char **listv, **metav, *map, *str, *file;
    int listc, metac, new, fd, i;
    Tcl_HashEntry *hPtr;
    Av *avPtr;
    Ns_Cache *cache;
    struct stat st;
    Ns_DString ds;
    Ns_Entry *entry;
    static uint32_t nextid;
    int done = 0;

    /*
     * Check the table of already mapped av's.
     */

    hPtr = Tcl_CreateHashEntry(&dataPtr->avTable, list, &new);
    if (!new) {
	*avPtrPtr = Tcl_GetHashValue(hPtr);
	return TCL_OK;
    }

    /*
     * Attempt to get the av from the global cache, mapping it if necessary
     */

    Ns_DStringInit(&ds);
    avPtr = NULL;
    file = AvPath(&ds, list);
    if (stat(file, &st) != 0) {
	Tcl_AppendResult(interp, "could not stat \"", file,
	    "\": ", Tcl_PosixError(interp), NULL);
	goto err1;
    }

    cache = AvCache();
    Ns_CacheLock(cache);
    entry = Ns_CacheCreateEntry(cache, list, &new);
    if (!new) {
        while (!new && (avPtr = Ns_CacheGetValue(entry)) == NULL) {
            Ns_CacheWait(cache);
            entry = Ns_CacheCreateEntry(cache, list, &new);
        }
        if (!new &&
		(avPtr->st.st_size != st.st_size ||
		 avPtr->st.st_mtime != st.st_mtime)) {
	    Ns_CacheUnsetValue(entry);
	    avPtr = NULL;
	    new = 1;
	}
    }
    if (new) {
    	/*
	 * Create a new list entry.
	 */
	 
	Ns_CacheUnlock(cache);
	fd = open(file, O_RDWR);
	if (fd < 0) {
	    Tcl_AppendResult(interp, "could not open \"", file,
		"\": ", Tcl_PosixError(interp), NULL);
	    goto err2;
	}
	map = mmap(NULL, (size_t) st.st_size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == (char *) -1) {
	    Tcl_AppendResult(interp, "could not mmap \"", file,
		"\": ", Tcl_PosixError(interp), NULL);
	    goto err2;
	}

	/*
	 * Skip header and padding.
	 */

        //bug 172957
        for (str = map; 
                (str < (map + st.st_size)) && *str != '\0'; str++){

            if(*str == '\n' && ((str + 2) <= (map + st.st_size))){
                if(*(str+1) == '\f' && *(str+2) == '\n'){
                    done = 1;
                    break;
                }
            }
        }

        if( !done ){
                Tcl_AppendResult(interp, "could not find header \"", file,
                "\": ", Tcl_PosixError(interp), NULL);
            munmap(map, st.st_size);
            goto err2;
        }
        //bug 172957

	i = (((str - map) / 8) + 1) * 8;

	/*
	 * Swap string lengths for pointers.
	 */

	metav = (char **) (map + i);
	metac = ntohl((uint32_t) metav[0]);
	listv = metav + metac + 1;
	listc = ntohl((uint32_t) listv[0]);
	str = (char *) &listv[listc+1];
	for (i = 0; i < metac; ++i) {
	    metav[i] = str;
	    str += ntohl((uint32_t) metav[i+1]);
	}
	for (i = 0; i < listc; ++i) {
	    listv[i] = str;
	    str += ntohl((uint32_t) listv[i+1]);
	}
	metav[metac] = listv[listc] = NULL;
	avPtr = (Av *) map;
	avPtr->refcnt = 1;
	avPtr->st = st;
	avPtr->meta.nelem = metac / 2;
	avPtr->meta.elems = (Dci_Elem *) metav;
	avPtr->data.nelem = listc / 2;
	avPtr->data.elems = (Dci_Elem *) listv;
	if (debug) {
	    Ns_Log(Notice, "sl[%s]: loaded, slots = %d ", list, avPtr->data.nelem);
	}
	Ns_CacheLock(cache);
        /*
         * Must re-get cache entry, creating if necessary; since we unlocked
         * the cache, ANYTHING could have happened to the entry that we once
         * had.  Can't count on it to still be in the cache.
         * There is a boundry condition within this case: if the entry we
         * are loading does expire out, and another thread requests it, that
         * other thread will also create a cache entry.  This is ok, in that
         * setting the entry cleans up what was already there.
         */
        entry = Ns_CacheCreateEntry(cache, list, &new);
	Ns_CacheSetValueSz(entry, avPtr, 1);
	avPtr->name = Ns_CacheKey(entry);
	if (++nextid == 0) {
	    ++nextid;
	}
	avPtr->id = nextid;
    }
    ++avPtr->refcnt;
err2:
    if (avPtr == NULL) {
	 Ns_CacheDeleteEntry(entry);
    }
    Ns_CacheBroadcast(cache);
    Ns_CacheUnlock(cache);
err1:
    Ns_DStringFree(&ds);
    if (avPtr == NULL) {
	Tcl_DeleteHashEntry(hPtr);
	return TCL_ERROR;
    }
    Tcl_SetHashValue(hPtr, avPtr);
    *avPtrPtr = avPtr;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvGetAuto --
 *
 *      Get the Av for the auto-loaded procs.
 *
 * Results:
 *      TCL_OK if auto-load disabled or Av found, TCL_ERROR otherwise.
 *
 * Side effects:
 *  	If the Av has changed since the last call, delete a procs
 *	previously created.  If avPtrPtr is non-NULL it will be 
 *	updated with pointer to Av.
 *
 *----------------------------------------------------------------------
 */

static int
AvGetAuto(AvData *dataPtr, Tcl_Interp *interp, Av **avPtrPtr)
{
    AvProc **procPtrPtr;
    Av *avPtr;

    if (dataPtr->autoname == NULL) {
	avPtr = NULL;
    } else if (AvGet(dataPtr, interp, dataPtr->autoname, &avPtr) != TCL_OK) {
	return TCL_ERROR;
    } else if (avPtr->id != dataPtr->autoid) {
        procPtrPtr = (AvProc **) dataPtr->procs.string;
        while (dataPtr->nprocs > 0) {
	    /* NB: Delete callback will remove cmd from array. */
	    Tcl_DeleteCommandFromToken(interp, procPtrPtr[0]->cmd);
	}
	dataPtr->autoid = avPtr->id;
    }
    if (avPtrPtr != NULL) {
    	*avPtrPtr = avPtr;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AvCleanup --
 *
 *      Cleanup any reference to av's in this interp.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	See AvFree.
 *
 *----------------------------------------------------------------------
 */

static void
AvCleanup(Tcl_Interp *interp, AvData *dataPtr)
{
    Av *avPtr;

    AvRelease(dataPtr);
    if (AvGetAuto(dataPtr, interp, &avPtr) == TCL_OK && avPtr != NULL) {
	AvRelease(dataPtr);
    }
}

static void
AvRelease(AvData *dataPtr)
{
    Tcl_HashTable *tablePtr = &dataPtr->avTable;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Ns_Cache *cache;
    Av *avPtr;

    if (tablePtr->numEntries > 0) {
	cache = AvCache();
    	Ns_CacheLock(cache);
    	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	while (hPtr != NULL) {
	    avPtr = Tcl_GetHashValue(hPtr);
	    AvFree(avPtr);
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Ns_CacheUnlock(cache);
	Tcl_DeleteHashTable(tablePtr);
	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AvAtCleanup --
 *
 *      Invoke the av.cleanup command at aolserver interp cleanup,
 *	normally after a connection.
 *
 * Results:
 *      Result of av.cleanup eval.
 *
 * Side effects:
 *  	See AvCleanup.
 *
 *----------------------------------------------------------------------
 */

static int
AvAtCleanup(Tcl_Interp *interp, void *ignored)
{
    return Tcl_EvalEx(interp, "av.cleanup", -1, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * AvFree --
 *
 *      Free an av.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will unmap the av on last reference.
 *
 *----------------------------------------------------------------------
 */

static void
AvFree(void *arg)
{
    Av *avPtr = arg;

    if (--avPtr->refcnt == 0
	    && munmap((char *) avPtr, (size_t) avPtr->st.st_size) != 0) {
        Ns_Fatal("av: munmap() failed: %s", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AvCache --
 *
 *      Return the Av cache, creating it if necessary.
 *
 * Results:
 *      Pointer to Ns_Cache.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static Ns_Cache *
AvCache(void)
{
    static volatile int initialized = 0;
    static Ns_Cache *cache;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
    	    cache = Ns_CacheCreateSz("dci:av", TCL_STRING_KEYS,
				     (size_t) maxlists, AvFree);
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }
    return cache;
}


/*
 *----------------------------------------------------------------------
 *
 * AvPath --
 *
 *      Constructs full av file path for given av.
 *
 * Results:
 *      Pointer to pathname.
 *
 * Side effects:
 *  	Will write path to given dstring.
 *
 *----------------------------------------------------------------------
 */

static char *
AvPath(Ns_DString *dsPtr, char *file)
{
    char *path;

    if (moddir != NULL && *file != '/') {
	path = Ns_MakePath(dsPtr, moddir, file, NULL);
    } else {
	path = Ns_DStringAppend(dsPtr, file);
    }
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * AvProcDelete --
 *
 *      Auto-loaded proc delete wrapper.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will remove given procPtr from interp's list of auto-loaded
 *	procs.
 *
 *----------------------------------------------------------------------
 */

static void
AvProcDelete(ClientData arg)
{
    AvProc *procPtr = arg;
    AvProc *swapPtr, **procsPtrPtr;
    AvData *dataPtr = procPtr->dataPtr;

    /*
     * Invoke the original proc delete.
     */

    if (procPtr->deleteProc != NULL) {
	(*procPtr->deleteProc)(procPtr->deleteData);
    }

    /*
     * Swap the last proc in the array with this one and truncate the array.
     */

    procsPtrPtr = (AvProc **) dataPtr->procs.string;
    swapPtr = procsPtrPtr[--dataPtr->nprocs];
    swapPtr->pidx = procPtr->pidx;
    procsPtrPtr[procPtr->pidx] = swapPtr;
    Tcl_DStringSetLength(&dataPtr->procs, dataPtr->nprocs * sizeof(AvProc *));
    ns_free(procPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AvAssocDelete --
 *
 *      Tcl assoc data callback to cleanup per-interp Av data.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will free any remaining av's.
 *
 *----------------------------------------------------------------------
 */

static void
AvAssocDelete(ClientData arg, Tcl_Interp *interp)
{
    AvData *dataPtr = arg;

    AvCleanup(interp, dataPtr);
    Tcl_DeleteHashTable(&dataPtr->avTable);
    Tcl_DStringFree(&dataPtr->procs);
    ns_free(dataPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendHdr --
 *
 *      Appends an av file header to given dstring.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Appends header to dstring.
 *
 *----------------------------------------------------------------------
 */

static void
AppendHdr(Tcl_DString *dsPtr, char *file, Dci_List *metaPtr, Dci_List *dataPtr)
{
    char buf[40];
    time_t now;
    int i, n;

    /*
     * Append the ascii header with create info and copy of meta data.
     */

    time(&now);
    Tcl_DStringInit(dsPtr);
    Tcl_DStringAppend(dsPtr, "!av", -1);
    Tcl_DStringAppend(dsPtr, "\n# ident: ", -1);
    Tcl_DStringAppend(dsPtr, rcsid, -1);
    Tcl_DStringAppend(dsPtr, "\n# file:  ", -1);
    Tcl_DStringAppend(dsPtr, file, -1);
    Tcl_DStringAppend(dsPtr, "\n# built: ", -1);
    Tcl_DStringAppend(dsPtr, ns_ctime(&now), -1);
    Tcl_DStringAppend(dsPtr, "# host:  ", -1);
    Tcl_DStringAppend(dsPtr, Ns_InfoHostname(), -1);
    Tcl_DStringAppend(dsPtr, "\n# nkeys: ", -1);
    sprintf(buf, "%d", dataPtr->nelem);
    Tcl_DStringAppend(dsPtr, buf, -1);
    Tcl_DStringAppend(dsPtr, "\n# nmeta: ", -1);
    sprintf(buf, "%d", metaPtr->nelem);
    Tcl_DStringAppend(dsPtr, buf, -1);
    Tcl_DStringAppend(dsPtr, "\n# meta:", -1);

    /*
     * Dump meta data so it can be visiable with 'more' command,
     * stopping after 10 items.
     */

    for (i = 0; i < metaPtr->nelem && i < 10; ++i) {
	Tcl_DStringAppend(dsPtr, "\n#  ", -1);
	AppendMeta(dsPtr, Dci_ListKey(metaPtr, i), 20);
	AppendMeta(dsPtr, Dci_ListValue(metaPtr, i), 60);
    }
    if (i < metaPtr->nelem) {
	Tcl_DStringAppend(dsPtr, "\n#  ...", -1);
    }
    Tcl_DStringAppend(dsPtr, "\n#\n\f\n", -1);

    /*
     * Pad to 8-byte boundry with a minimum size of the in-memory Av struct.
     */

    n = dsPtr->length;
    if (n < sizeof(Av)) {
	n = sizeof(Av);
    }
    i = ((n / 8) + 1) * 8;
    Tcl_DStringSetLength(dsPtr, i);
    memset(dsPtr->string + n, 0, i - n);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendMeta --
 *
 *      Append a meta data section key or value to the header.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will truncate string to max size or first newline both
 *	to keep it readable and to avoid a possible \n\f\n end
 *	of headers sequence.
 *
 *----------------------------------------------------------------------
 */

static void
AppendMeta(Tcl_DString *dsPtr, char *str, int max)
{
    int n, len;
    char *nl;

    n = len = strlen(str);
    if (n > max) {
	n = max;
    }
    nl = strchr(str, '\n');
    if (nl != NULL && (nl - str) < n) {
	n = nl - str;
    }
    Tcl_DStringStartSublist(dsPtr);
    Tcl_DStringAppend(dsPtr, str, n);
    if (n < len) {
	Tcl_DStringAppend(dsPtr, " ...", 4);
    }
    Tcl_DStringEndSublist(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendLengths --
 *
 *      Append section lengths array to header.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will append binary array to header and set given offPtr and
 *	lenPtr to array offset and total string length.
 *
 *----------------------------------------------------------------------
 */

static void
AppendLengths(Tcl_DString *dsPtr, Dci_List *listPtr, int *offPtr, int *lenPtr)
{
    int i, len, off, listc, hlen;
    char **listv;
    uint32_t *up;
    size_t n;

    /*
     * Append an array of uint32_t with string lengths and accumulate
     * total length.
     */

    listc = listPtr->nelem * 2;
    listv = (char **) listPtr->elems;
    len = 0;
    off = dsPtr->length;
    hlen = sizeof(uint32_t) * (listc + 1);
    Tcl_DStringSetLength(dsPtr, off + hlen);
    up = (uint32_t *) (dsPtr->string + off);
    up[0] = htonl(listc);
    for (i = 0; i < listc; ++i) {
	n = strlen(listv[i]) + 1;
	up[i+1] = (uint32_t) n;
	len    += (int) n;
    }
    *offPtr = off;
    *lenPtr = len;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendStrings --
 *
 *      Append actual strings to given dstring.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Will update lengths at given offset to network byte order
 *	and append strings starting at given len.
 *
 *----------------------------------------------------------------------
 */

static void
AppendStrings(Tcl_DString *dsPtr, Dci_List *listPtr, int off, int len)
{
    char *base, **listv;
    uint32_t *up, n;
    int i, listc;

    /*
     * Extend dstring with space for strings, copy each string and upate
     * lengths array to network byte order.
     */

    listc = listPtr->nelem * 2;
    listv = (char **) listPtr->elems;
    i = dsPtr->length;
    Tcl_DStringSetLength(dsPtr, i + len);
    base = dsPtr->string + i;
    up = (uint32_t *) (dsPtr->string + off);
    for (i = 0; i < listc; ++i) {
	n = up[i+1];
    	up[i+1] = htonl(n);
	memcpy(base, listv[i], (size_t) n);
	base += n;
    }
}
