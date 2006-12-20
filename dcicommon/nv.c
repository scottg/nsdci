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

/*
 * nv.c --
 *
 *      This file implements the commands used for networked variables.
 *      The systems administrator must configure these in th AOL Server's
 *      configuration file.
 *      NV arrays are implemented in sets where each set has its own collection
 *      of array names, a list of clients and a tcp/ip port.
 *
 */

static char rcsid[] = "$Id$";

#include "dci.h"

/*
 * The following define Array flag bits.
 */
 
#define NV_PERSIST 1    /* Write contents to persist file. */
#define NV_FSYNC   2    /* Flush persist file with fsync. */
#define NV_FLUSH   4	/* Flush to disk when ready. */
#define NV_INIT    8	/* Re-generate net init message. */
#define NV_DIRTY   (NV_FLUSH|NV_INIT)

/*
 * The following are options for SetVar, UnsetVar, and FlushArray.
 */
 
#define NV_CSET      16
#define NV_BROADCAST 32

/*
 * The following structure maintain context for each array.
 */

struct Server;

typedef struct Array {
    char *name;               /* Name of array. */
    Ns_Mutex lock;            /* Lock protecting access to variables. */
    Tcl_HashTable vars;       /* Table of variable key/value pairs. */
    struct Array *nextPtr;    /* Next in list of arrays to flush. */

    /*
     * The following elements maintain state for an array to
     * be broadcast to clients through a server.
     */

    char *netname;            /* Network name. */
    struct Server *servPtr;   /* Broadcaster server, if any. */
    Ns_DString log;	      /* Update log to broadcast. */

    /*
     * The following elements maintain stats of an array.
     */

    int sizekeys;             /* Total string length of keys. */
    int sizevalues;           /* Total string length of values. */
    int nget;                 /* # of gets */
    int nmiss;                /* # of get misses */
    int nset;                 /* # of sets */
    int nappend;              /* # of appends */
    int nlappend;             /* # of lappends */
    int nunset;               /* # of unsets */
    int nflush;               /* # of disk flushes */
    int ndump;                /* # of dumps */
    int nload;                /* # of loads */
    int flags;                /* Flags bits defined above */
} Array;

/*
 * The following maintains the context for a variable server.
 */

typedef struct Server {
    char *name;     	    /* Server config name. */
    Dci_Broadcaster *bcast;
    Dci_Msg *init;	    /* Init msg for client connect sync. */
    Array *arrays[1];	    /* List of arrays. */
} Server;

/*
 * The following structure maintains context for each variable
 * client.
 */

typedef struct Client {
    char *name;     	    /* Client config name. */
    Tcl_HashTable arrays;   /* Maps netnames to local names. */
} Client;

/*
 * Forward declarations
 */

static Tcl_CmdProc NvFileCmd, NvArraysCmd, NvIncrCmd, NvLoadCmd,
	NvFlushCmd, DefunctCmd, NvSetCmd, NvAppendCmd, NvGetCmd,
	NvUnsetCmd, NvDumpCmd, NvCreateCmd, NvStatsCmd, NvSleepCmd;

static Dci_RecvProc RecvUpdate;
static Ns_ThreadProc FlusherThread;
static Ns_Callback StopFlusher;
static int SetVar(Array *arrayPtr, char *key, char *value, int flags);
static int UnsetVar(Array *arrayPtr, char *key, int flags);
static void FlushArray(Array *arrayPtr, int flags);
static char *GetFile(char *name, char *buf);
static void AppendReload(char **basePtr, Array *arrayPtr);
static void BroadcastUpdates(Array *arrayPtr);
static int AddStat(Tcl_Interp *interp, char *var, char *name, char *value);
static int AddStatInt(Tcl_Interp *interp, char *var, char *name, int value);
static Dci_Msg *ClientInitMsg(void *clientData);
static int CreateArray(char *name, int flags);
static int GetArray(Tcl_Interp *interp, char *array, Array **arrayPtrPtr);

static Tcl_HashTable arrays;
static int fDebug;

/*
 * The following structure maintains state for the persistant file
 * background flusher.
 */

struct {
    Ns_Mutex lock;
    Ns_Cond cond;
    Ns_Thread thread;
    int stop;
    int sleep;
    Array *firstPtr;
} flush;


/*
 *----------------------------------------------------------------------
 *
 * DciNvInit --
 *
 *      Process the correct section(s) of the AOL Server configuration file
 *      and create the appropriate client or server networked variable (NV)
 *      sets.
 *
 * Results:
 *      The return vaue is normally NS_OK.
 *
 * Side effects:
 *      May create one or more servers, clients, and/or variables.
 *     
 *----------------------------------------------------------------------
 */

void
DciNvLibInit(void)
{
    DciAddIdent(rcsid);
    Tcl_InitHashTable(&arrays, TCL_STRING_KEYS);
}

int
DciNvModInit(char *server, char *module)
{
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;
    Server *servPtr;
    Client *clientPtr;
    Ns_Set *set, *clients, *nvs;
    char *login, *path, *name, *array, *netname;
    int i, flags, opt, j, n, new;
    Ns_DString dir;

    path = Ns_ConfigGetPath(server, module, "nv", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
        fDebug = 0;
    }
    if (!Ns_ConfigGetInt(path, "flushinterval", &flush.sleep)) {
        flush.sleep = 30;
    }
    Ns_DStringInit(&dir);
    Ns_MakePath(&dir, dciDir, "nv", NULL);
    if (mkdir(dir.string, 0755) != 0 && errno != EEXIST) {
	Ns_Log(Error, "nv: mkdir(%s) failed: %s", dir.string, strerror(errno));
	return NS_ERROR;
    }
    Ns_DStringFree(&dir);

    /*
     * Create pre-configured arrays.
     */

    path = Ns_ConfigGetPath(server, module, "nv/arrays", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
    	flags = 0;
    	name = Ns_SetKey(set, i);
    	path = Ns_ConfigGetPath(server, module, "nv/array", name, NULL);
	if (!Ns_ConfigGetInt(path, "persist", &opt)) {
	    opt = 0;
	}
	if (opt) {
	    flags |= NV_PERSIST;
	}
	if (!Ns_ConfigGetBool(path, "fsync", &opt)) {
	    opt = 1;
	}
	if (opt) {
	    flags |= NV_FSYNC;
	}
	if (!CreateArray(name, flags)) {
    	    return NS_ERROR;
	}
    }
    
    /*
     * Create servers, if any.
     */

    path = Ns_ConfigGetPath(server, module, "nv/servers", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	name = Ns_SetKey(set, i);
	path = Ns_ConfigGetPath(server, module, "nv/server", name, NULL);
	login = Ns_ConfigGetValue(path, "login");
        if (fDebug) {
            Ns_Log(Notice,"Configuring nv server %s",name);
        }
	if (login == NULL) {
	    login = name;
	}
	path = Ns_ConfigGetPath(server, module, "nv/server", name, "clients", NULL);
	clients = Ns_ConfigGetSection(path);
	path = Ns_ConfigGetPath(server, module, "nv/server", name, "arrays", NULL);
	nvs = Ns_ConfigGetSection(path);
	if (clients != NULL && nvs != NULL) {
	    n = Ns_SetSize(nvs);
	    servPtr = ns_malloc(sizeof(Server) + sizeof(Array *) * n);
	    servPtr->name = name;
	    servPtr->init = NULL;
	    servPtr->bcast = Dci_CreateBroadcaster(login, servPtr, clients, ClientInitMsg);
	    if (servPtr->bcast == NULL) {
		return NS_ERROR;
	    }
	    for (j = 0; j < n; ++j) {
		array = Ns_SetKey(nvs, j);
		netname = Ns_SetValue(nvs, j);
		if (GetArray(NULL, array, &arrayPtr) != TCL_OK) {
		    Ns_Log(Error, "nvs[%s]: no such array: %s", name, array);
		    return NS_ERROR;
		}
		if (arrayPtr->servPtr != NULL) {
	    	    Ns_Log(Error, "nvs[%s]: dup: %s = %s", name, array, netname);
		    return NS_ERROR;
		}
		servPtr->arrays[j] = arrayPtr;
		arrayPtr->servPtr = servPtr;
		arrayPtr->netname = netname;
	    }
	    servPtr->arrays[j] = NULL;
	}
    }

    /*
     * Create client end points, if any.
     */

    path = Ns_ConfigGetPath(server, module, "nv/clients", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	name = Ns_SetKey(set, i);
	path = Ns_ConfigGetPath(server, module, "nv/client", name, NULL);
	login = Ns_ConfigGetValue(path, "login");
        if ( fDebug ) {
            Ns_Log(Notice,"Configuring nv client %s",name);
        }
	if (login == NULL) {
	    login = name;
	}
	clientPtr = ns_malloc(sizeof(Client));
	clientPtr->name = name;
	Tcl_InitHashTable(&clientPtr->arrays, TCL_STRING_KEYS);
	path = Ns_ConfigGetPath(server, module, "nv/client", name, "arrays", NULL);
	nvs = Ns_ConfigGetSection(path);
	for (j = 0; nvs != NULL && j < Ns_SetSize(nvs); ++j) {
	    array = Ns_SetKey(nvs, j);
	    netname = Ns_SetValue(nvs, j);
	    if (GetArray(NULL, array, &arrayPtr) != TCL_OK) {
		Ns_Log(Error, "nvc[%s]: no such array: %s", name, array);
		return NS_ERROR;
	    }
	    hPtr = Tcl_CreateHashEntry(&clientPtr->arrays, netname, &new);
	    if (!new) {
		Ns_Log(Error, "nvc[%s]: dup: %s = %s", name, array, netname);
		return 0;
	    }
	    Tcl_SetHashValue(hPtr, arrayPtr);
	}
        if (Dci_CreateReceiver(login, RecvUpdate, clientPtr) != NS_OK) {
            return NS_ERROR;
        }
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DciNvTclInit --
 *
 *      Ns_TclInitInterps() callback to add Tcl commands.
 *
 * Results:
 *      Standard Tcl result code; TCL_OK.
 *
 * Side effects:
 *      Tcl commands are added to the master interpreter procedure 
 *      table.
 *
 *----------------------------------------------------------------------
 */

int
DciNvTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "nv.connect", DefunctCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.listen",  DefunctCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.create",  NvCreateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.debug",   DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nv.file",    NvFileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.arrays",  NvArraysCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.set",     NvSetCmd, (ClientData) 's', NULL);
    Tcl_CreateCommand(interp, "nv.cset",    NvSetCmd, (ClientData) 'c', NULL);
    Tcl_CreateCommand(interp, "nv.append",  NvAppendCmd, (ClientData) 'a',NULL);
    Tcl_CreateCommand(interp, "nv.lappend", NvAppendCmd, (ClientData) 'l',NULL);
    Tcl_CreateCommand(interp, "nv.get",     NvGetCmd, (ClientData) 'g', NULL);
    Tcl_CreateCommand(interp, "nv.cget",    NvGetCmd, (ClientData) 'c', NULL);
    Tcl_CreateCommand(interp, "nv.tget",    NvGetCmd, (ClientData) 't', NULL);
    Tcl_CreateCommand(interp, "nv.exists",  NvGetCmd, (ClientData) 'e', NULL);
    Tcl_CreateCommand(interp, "nv.incr",    NvIncrCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.load",    NvLoadCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.flush",   NvFlushCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.unset",   NvUnsetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.names",   NvDumpCmd, (ClientData) 'n', NULL);
    Tcl_CreateCommand(interp, "nv.dump",    NvDumpCmd, (ClientData) 'd', NULL);
    Tcl_CreateCommand(interp, "nv.stats",   NvStatsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nv.setSleep",NvSleepCmd, NULL, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvGetCmd --
 *
 *      Returns value found in an NV array's hash element. This
 *      function implements the nv.get, nv.cget, nv.tget and
 *      nv.exists commands.
 *
 *      Command: nv.get <NV_ARRAY_NAME> <Key>
 *               Passes the value contained in the <NV_ARRAY_NAME>
 *               in the <Key> to the tcl interpreter.
 *
 *      Example: nv.get Haystack needle
 *               Returns the value contained in the "needle" element
 *               from the NV array named Haystack.
 *
 *      Command: nv.cget <NV_ARRAY_NAME> <Key> <VarName>
 *               Passes the value contained in the <NV_ARRAY_NAME>
 *               in the <Key> and assigns it to the varaible <VarName>.
 *
 *      Example: nv.set Haystack needle "Is small and sharp!"
 *               nv.cget Haystack needle putTheValueHere
 *               The variable putTheValueHere contains "Is small and sharp!"
 *
 *      Command: nv.tget <NV_ARRAY_NAME> <Key>
 *               Passes the value contained in the <NV_ARRAY_NAME>
 *               in the <Key>, without leading and trailing spaces,
 *               to the tcl interpreter.
 *
 *      Example: nv.set Haystack needle "   Is small and sharp!   "
 *               nv.tget Haystack needle
 *
 *               The value returned is "Is small and sharp!".
 *
 *      Command: nv.exists <NV_ARRAY_NAME> <Key>
 *
 *      Example: nv.exists Haystack needle
 *               Checks for the existance of a needle element in
 *               the Haystack NV array.
 *
 * Results:
 *      TCL_OK if element does not exist
 *      TCL_ERROR if element exists
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------
 */

static int
NvGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    int cmd = (int) arg;
    char *value;
    Array *arrayPtr;

    if ((cmd == 'c' && argc != 4) || (cmd != 'c' && argc != 3)) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array key", cmd == 'c' ? " varName\"" : "\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_MutexLock(&arrayPtr->lock);
    ++arrayPtr->nget;
    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, argv[2]);
    if (hPtr == NULL) {
        ++arrayPtr->nmiss;
    } else {
        value = Tcl_GetHashValue(hPtr);
        if (cmd == 'g') {
            Tcl_SetResult(interp, value, TCL_VOLATILE);
        } else if (cmd == 't') {
            Tcl_DString ds;

            Tcl_DStringInit(&ds);
            Tcl_DStringAppend(&ds, value, -1);
            value = Ns_StrTrim(ds.string);
            Tcl_SetResult(interp, value, TCL_VOLATILE);
            Tcl_DStringFree(&ds);
        } else {
            if (cmd == 'c' && Tcl_SetVar(interp, argv[3], value,
                    TCL_LEAVE_ERR_MSG) == NULL) {
                Ns_MutexUnlock(&arrayPtr->lock);
                return TCL_ERROR;
            }
            Tcl_SetResult(interp, "1", TCL_STATIC);
        }
    }
    Ns_MutexUnlock(&arrayPtr->lock);

    if (hPtr == NULL) {
        if (cmd == 'g') {
            Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
            return TCL_ERROR;
        } else if (cmd != 't') {
            Tcl_SetResult(interp, "0", TCL_STATIC);
        }
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvSetCmd --
 *
 *      Sets the value in an NV array's hash element.
 *
 *      Example: nv.set Haystack needle "Is small and sharp!"
 *               Sets the value contained in the "needle" element in the
 *               NV array named Haystack to the string "Is small and sharp!".
 *
 *      Example: nv.cset Haystack needle "Is small and sharp!"
 *               Conditionally sets the value contained in the "needle" element
 *               in the NV array named Haystack to the string
 *               "Is small and sharp!" provided that the needle element
 *               of the Haystack array does not exist.
 *
 * Results:
 *      TCL_ERROR for Failure
 *      TCL_OK for success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvSetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    int new, flags;

    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array key value\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    flags = NV_BROADCAST;
    if ((int) arg == 'c') {
	flags |= NV_CSET;
    }
    Ns_MutexLock(&arrayPtr->lock);
    new = SetVar(arrayPtr, argv[2], argv[3], flags);
    Ns_MutexUnlock(&arrayPtr->lock);
    if (flags & NV_CSET) {
        Tcl_SetResult(interp, new ? "1" : "0", TCL_STATIC);
    } else {
        Tcl_SetResult(interp, argv[3], TCL_VOLATILE);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvAppendCmd --
 *
 *      Appends to the string in an NV array's hash element.
 *      This function implements the nc.append and nv.lappend commands.
 *
 *      Command: nv.append <NV_ARRAY_NAME> <Key> <ToAppend>
 * 
 *      Example: Assuming the needle element of the Haystack array contains
 *               "Is small and sharp!"
 *               nv.append Haystack needle "Is also hard to find!"
 *               Modifies the value contained in the "needle" element
 *               in the NV array named Haystack to the string
 *               "Is small and sharp!Is also hard to find!".
 *
 *      Command: nv.lappend <NV_ARRAY_NAME> <Key> <ToAppend1> ... <ToAppendX>
 *
 *               Appends a list to the value contained in the <Key>
 *               element of <NV_ARRAY_NAME>.
 *
 *      Example: Assuming the needle element of the Haystack array contains
 *               "Is "
 *               nv.lappend Haystack needle small and sharp! Be Careful!
 *
 *               Causes the needle element of the NV named Haystack to
 *               contain "Is small and sharp! Be Careful!"
 *
 * Results:
 *      Returns TCL_OK on success.
 *      Returns TCL_ERROR on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvAppendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    int cmd = (int) arg;
    int i;
    char *value;
    Tcl_HashEntry *hPtr;

    if (argc < 4) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array key string ?string ...?\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_MutexLock(&arrayPtr->lock);
    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, argv[2]);
    if (hPtr != NULL) {
        value = Tcl_GetHashValue(hPtr);
        Tcl_SetResult(interp, value, TCL_VOLATILE);
    }
    for (i = 3; i < argc; ++i) {
        if (cmd == 'a') {
            Tcl_AppendResult(interp, argv[i], NULL);
        } else {
            Tcl_AppendElement(interp, argv[i]);
        }
    }
    SetVar(arrayPtr, argv[2], interp->result, NV_BROADCAST);
    if (cmd == 'a') {
	++arrayPtr->nappend;
    } else {
	++arrayPtr->nlappend;
    }
    Ns_MutexUnlock(&arrayPtr->lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvCreateCmd --
 *
 *      Creates an NV array hash. See warning below!
 *
 *      Examples: nv.create Haystack
 *                The NV array named Haystack is created (non-persist)
 *
 *                nv.create -persist Haystack
 *                The NV array named Haystack is created (persist/saved to disk)
 *
 *                nv.create -npersist Haystack
 *                The NV array named Haystack is created (persist/saved to disk)
 *                (Non-synchronized I/O).
 *
 * Results:
 *      TCL_OK is returned for success.
 *      TCL_ERROR is returned for:
 *          invalid use of this function.
 *          persistent nv file could not be opened.
 *          persistent nv file could not stated.
 *          persistent nv file is not a regular file.
 *          persistent nv file could not be read.
 *          unable to split the file into argc/argv.
 *          argc is odd; key/value pairs come in twos.
 *          attempting to create an array that exists.
 *
 * Side effects:
 *	Calling this function once the server is running is highly
 *      discouraged, will produce unpredictable results and can
 *      crash the server. A decision was made to accept this side effect
 *      insted of implementing a lock around updating and accessing
 *      the NV arrays (Hash table containing all the NV hashes).
 *      This was done in the name of performance.
 *
 *----------------------------------------------------------------
 */

static int
NvCreateCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int flags;
    char *name;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " ?-persist|-npersist? array\"", NULL);
        return TCL_ERROR;
    }
    name = argv[argc-1];
    if (argc == 2) {
	flags = 0;
    } else if (STREQ(argv[1], "-npersist")) {
	flags = NV_PERSIST;
    } else if (STREQ(argv[1], "-persist")) {
	flags = NV_PERSIST | NV_FSYNC;
    } else {
	Tcl_AppendResult(interp, "unknown option \"", argv[1],
	    "\": should be -persist or -npersist", NULL);
	return TCL_ERROR;
    }
    if (!CreateArray(name, flags)) {
        Tcl_AppendResult(interp, "could not create array: ", name, NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvUnsetCmd --
 *
 *      Eliminates an NV array's hash element.
 *
 *	Example: nv.unset Haystack needle
 *               Eliminates the "needle" element
 *               from the NV array named Haystack.
 *
 * Results:
 *      TCL_OK is returned on success.
 *      TCL_ERROR is returned if:
 *          The array is not found.
 *          Incorrect usage.
 *          Unable to tell clients to unset the array's element.
 *
 * Side effects;
 *      Array element is removed from the array.
 *
 *----------------------------------------------------------------
 */

static int
NvUnsetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array key\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_MutexLock(&arrayPtr->lock);
    UnsetVar(arrayPtr, argv[2], NV_BROADCAST);
    Ns_MutexUnlock(&arrayPtr->lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvDumpCmd --
 *
 *      Returns Tcl Key/Value (AGF compatible) string representing
 *      all of the hash's key/value pairs. Also returns the names of
 *      each of the arrays elements. This function implements nv.dump
 *      and nv.names.
 *
 *      Command: nv.dump <NV_ARRAY_NAME>
 *
 *      Example: nv.dump Haystack
 *
 *      Command: nv.names <NV_ARRAY_NAME> [patternInNamesToMatch]
 *
 *      Example: nv.names HayStack
 *               returns all the keys (aka names) in this NV.
 *
 *               nv.names Haystack needles*
 *               returns all the keys (aka names) in this NV
 *               that start with the pattern "needles".
 *
 *
 * Results:
 *      TCL_OK if success.
 *      TCL_ERROR for invalid usage or array not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *key, *pattern;
    int type = (int) arg;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    pattern = argv[2];
    Ns_MutexLock(&arrayPtr->lock);
    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
    while (hPtr != NULL) {
        key = Tcl_GetHashKey(&arrayPtr->vars, hPtr);
        if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
            Tcl_AppendElement(interp, Tcl_GetHashKey(&arrayPtr->vars, hPtr));
            if (type == 'd') {
                Tcl_AppendElement(interp, Tcl_GetHashValue(hPtr));
            }
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&arrayPtr->lock);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 *
 * NvFlushCmd --
 *
 *      Deletes all keys in the provided NV's hash or all
 *      the keys matching the key provided.
 *
 *      Command: nv.flush <NV_ARRAY_NAME>
 *
 *      Example: nv.flush Haystack 
 *
 *      Command: nv.flush <NV_ARRAY_NAME> <KEY>
 *
 *      Example: nv.flush Haystack needle*
 *               Destroys all the keys in th Haystack array
 *               begining with "needle".
 *
 * Results:
 *      TCL_OK if success.
 *      TCL_ERROR for invalid usage or array not found.
 *
 * Side effects:
 *      All the elements of the array are destroyed.
 *
 *----------------------------------------------------------------
 */

static int
NvFlushCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *key;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_MutexLock(&arrayPtr->lock);
    if (argc == 2) {
        FlushArray(arrayPtr, 0);
    } else {
        hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
        while (hPtr != NULL) {
            key = Tcl_GetHashKey(&arrayPtr->vars, hPtr);
            if (Tcl_StringMatch(key, argv[2])) {
                UnsetVar(arrayPtr, key, 0);
            }
            hPtr = Tcl_NextHashEntry(&search);
        }
    }
    BroadcastUpdates(arrayPtr);
    Ns_MutexUnlock(&arrayPtr->lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvIncrCmd --
 *
 *      Increments the value contained in an NV array's key's value.
 *
 *      Command: nv.incr <NV_ARRAY_NAME> <KEY> [IncrementByThisNumber]
 *
 *      Example: nv.incr Haystack numberOfNeedles
 *               The numberOfNeedles value is incremented.
 *
 *      Example: nv.incr Haystack numberOfNeedles 10
 *               The The numberOfNeedles value is incremented by 10.
 *
 * Results:
 *      TCL_OK on success.
 *      TCL_ERROR for invalid usage, non-integer provided as incrementer,
 *      array not found, key not found,
 *      or the value to be incremented s not an integer.
 *      
 *
 * Side effects:
 *      The value of the array's specified element is arithmetically
 *      adjusted.
 *
 *----------------------------------------------------------------
 */

static int
NvIncrCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    int count, current, result;
    char buf[20], *value;
    Tcl_HashEntry *hPtr;

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array key ?count?\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 3)  {
        count = 1;
    } else if (Tcl_GetInt(interp, argv[3], &count) != TCL_OK) {
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    result = TCL_ERROR;
    Ns_MutexLock(&arrayPtr->lock);
    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, argv[2]);
    if (hPtr != NULL) {
        value = Tcl_GetHashValue(hPtr);
        result = Tcl_GetInt(interp, value, &current);
        if (result == TCL_OK) {
            current += count;
            sprintf(buf, "%d", current);
            SetVar(arrayPtr, argv[2], buf, NV_BROADCAST);
        }
    }
    Ns_MutexUnlock(&arrayPtr->lock);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
    } else if (result == TCL_OK) {
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
    }
    return result;
}


/*
 *----------------------------------------------------------------
 * NvLoadCmd --
 *
 *      Defines all the elements and their values for the provided
 *      NV array. If -noflush is provided as an argument, existing
 *      keys are not destroyed prior to the load.
 *      In the case of a regualr load, all values are eliminated and then
 *      re-established.
 *
 *      Command: nv.load [-noflush] <NV_ARRAY_NAME> <LIST>
 *      Example:
 *          nv.load Haystack list
 *          nv.load -noflush Haystack list
 *
 *          nv.load weather [dci.readFile /tmp/weather.agf]
 *
 * Results:
 *      TCL_OK for success.
 *      TCL_ERROR for invalid usage, array not found or unable to set values.
 *
 * Side effects:
 *      The keys in the array are redefined.
 *
 *----------------------------------------------------------------
 */

static int
NvLoadCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    int largc, i, flush;
    char **largv;

    flush = 1;
    if (argc != 3 && argc != 4) {
badargs:
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " ?-noflush? array list\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 4) {
	if (!STREQ(argv[1], "-noflush")) {
            goto badargs;
	}
	flush = 0;
    }
    if (GetArray(interp, argv[argc-2], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_SplitList(interp, argv[argc-1], &largc, &largv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (largc & 1) {
        ckfree((char *) largv);
	Tcl_AppendResult(interp, "invalid nv list: odd # elements: ",
	    argv[argc-1], NULL);
        return TCL_ERROR;
    }
    Ns_MutexLock(&arrayPtr->lock);
    if (flush) {
        FlushArray(arrayPtr, 0);
    }
    for (i = 0; i < largc; i += 2) {
        SetVar(arrayPtr, largv[i], largv[i+1], 0);
    }
    ++arrayPtr->nload;
    BroadcastUpdates(arrayPtr);
    Ns_MutexUnlock(&arrayPtr->lock);
    ckfree((char *) largv);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvFileCmd --
 *
 *      Return the path where the nv persist file is/would be located.
 *
 *      Command: nv.file <NV_ARRAY_NAME>
 *
 *      Example: nv.file Haystack
 *               Sends <nv directory path>/<NV_ARRAY_NAME> to the interpreter
 *
 * Results:
 *      Sends <nv directory path>/<NV_ARRAY_NAME> to the interpreter
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvFileCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    char path[PATH_MAX];
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array\"", NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, GetFile(argv[1], path), TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvArraysCmd --
 *
 *      Returns a list of nv array names.
 *
 *      Example: nv.arrays
 *               Will list all nv array names.
 *
 *               nv.arrays Hay*
 *               Returns all nv array names that begin with "Hay".
 *                
 * Results:
 *      TCL_OK on success.
 *      TCL_ERROR for invalid usage.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvArraysCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Array *arrayPtr;
    char *pattern;

    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    pattern = argv[1];

    hPtr = Tcl_FirstHashEntry(&arrays, &search);
    while (hPtr != NULL) {
        arrayPtr = Tcl_GetHashValue(hPtr);
        if (pattern == NULL || Tcl_StringMatch(arrayPtr->name, pattern)) {
            Tcl_AppendElement(interp, arrayPtr->name);
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 * NvStatsCmd --
 *
 *      Places an NV array's statistics into a statistic array's elements.
 *      The defined statistics array's elements are named in the third argument
 *      of AddStatInt() and AddStat(). The elements set in the
 *      statistics array are flags, keys, keysize, valuesize, nget,
 *      nappend, nlappend, nmiss, nset, nunset, nload, ndump, nflush and clients.
 *      
 *
 *      Example: nv.stats Haystack SomeArrayNameToHoldStatistics
 *      set temp $SomeArrayNameToHoldStatistics(keys) sets the
 *      number of keys containing in the NV to temp.
 *
 * Results:
 *      TCL_OK on success.
 *      TCL_ERROR for invalid usage, invalid array name or
 *      unable to set values in the provided statistics array.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
NvStatsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    char *var;
    int status;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
                argv[0], " array varName\"", NULL);
        return TCL_ERROR;
    }
    if (GetArray(interp, argv[1], &arrayPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    var = argv[2];
    Ns_MutexLock(&arrayPtr->lock);
    status = TCL_ERROR;
    do {
        if (!AddStatInt(interp, var, "flags", arrayPtr->flags)) {
            break;
        }
        if (!AddStatInt(interp, var, "keys", arrayPtr->vars.numEntries)) {
            break;
        }
        if (!AddStatInt(interp, var, "keysize", arrayPtr->sizekeys)) {
            break;
        }
        if (!AddStatInt(interp, var, "valuesize", arrayPtr->sizevalues)) {
            break;
        }
        if (!AddStatInt(interp, var, "totalsize",
            arrayPtr->sizekeys + arrayPtr->sizevalues)) {
            break;
        }
        if (!AddStatInt(interp, var, "nget", arrayPtr->nget)) {
            break;
        }
        if (!AddStatInt(interp, var, "nappend", arrayPtr->nappend)) {
            break;
        }
        if (!AddStatInt(interp, var, "nlappend", arrayPtr->nlappend)) {
            break;
        }
        if (!AddStatInt(interp, var, "nmiss", arrayPtr->nmiss)) {
            break;
        }
        if (!AddStatInt(interp, var, "nset", arrayPtr->nset)) {
            break;
        }
        if (!AddStatInt(interp, var, "nunset", arrayPtr->nunset)) {
            break;
        }
        if (!AddStatInt(interp, var, "nload", arrayPtr->nload)) {
            break;
        }
        if (!AddStatInt(interp, var, "ndump", arrayPtr->ndump)) {
            break;
        }
        if (!AddStatInt(interp, var, "nflush", arrayPtr->nflush)) {
            break;
        }
        if (!AddStat(interp, var, "netname",
		arrayPtr->netname ? arrayPtr->netname : "")) {
            break;
        }
        if (!AddStat(interp, var, "server",
		arrayPtr->servPtr ? arrayPtr->servPtr->name : "")) {
            break;
        }
        status = TCL_OK;
    } while (0);
    Ns_MutexUnlock(&arrayPtr->lock);
    return status;
}


/*
 *----------------------------------------------------------------
 * NvSleepCmd --
 *
 *     Resets the number of seconds the flusher thread sleeps between
 *     flush cycles; periods between checking to see if each NV array's
 *     data has been modified and needs to be written to disk. If the
 *     NumberOfSecondsToSleepBetweenFlushCycles argument is not provided,
 *     the routin only returns the current number of seconds between
 *     flush cycles.
 *
 *     Command: nv.setSleep [NumberOfSecondsToSleepBetweenFlushCycles]
 *
 * Results:
 *     Returns the NumberOfSecondsToSleepBetweenFlushCycles to the
 *     tcl interpreter.
 *     TCL_OK is returned for success.
 *     TCL_ERROR is returned for nvalid usage.
 *
 *
 * Side effects:
 *     None.
 *
 *----------------------------------------------------------------
 */

static int
NvSleepCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[32];
    int new, last;

    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " ?seconds?\"", NULL);
        return TCL_ERROR;
    }
    if (argc > 1 && Tcl_GetInt(interp, argv[1], &new) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_MutexLock(&flush.lock);
    last = flush.sleep;
    if (argc > 1) {
	flush.sleep = new;
    }
    Ns_MutexUnlock(&flush.lock);
    sprintf(buf, "%d", last);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateArray --
 *
 *      Create a new nv array, initializing with the persist data file 
 *	if any.
 *
 * Results:
 *      1 if array created, 0 otherwise.
 *
 * Side effects:
 *      None.
 *     
 *----------------------------------------------------------------------
 */

static int
CreateArray(char *name, int flags)
{
    Array *arrayPtr;
    int new, fd, i, largc;
    size_t len;
    char path[PATH_MAX], **largv, *loadbuf;
    struct stat st;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_CreateHashEntry(&arrays, name, &new);
    if (!new) {
    	Ns_Log(Warning, "nv: duplicate array: %s", name);
	return NS_ERROR;
    }
    arrayPtr = NULL;
    loadbuf = NULL;
    fd = -1;
    largv = NULL;
    if (flags & NV_PERSIST) {
        fd = open(GetFile(name, path), O_RDWR|O_CREAT, 0644);
        if (fd < 0) {
            Ns_Log(Warning, "nv: open(%s) failed: %s", path, strerror(errno));
            goto fail;
        }
        if (fstat(fd, &st) != 0) {
            Ns_Log(Warning, "nv: fstat(%s) failed: %s", path, strerror(errno));
            goto fail;
        }
        if (!S_ISREG(st.st_mode)) {
            Ns_Log(Warning, "nv: not an ordinary file: %s", path);
            goto fail;
        }
        if (st.st_size > 0) {
            len = st.st_size;
            loadbuf = ns_malloc(len+1);
            if (read(fd, loadbuf, len) != len) {
		Ns_Log(Warning, "nv: read of %s failed: %s", path, strerror(errno));
                goto fail;
            }
            if (loadbuf[len-1] == '\n') {
                --len;
            }
            loadbuf[len] = '\0';
            if (Tcl_SplitList(NULL, loadbuf, &largc, &largv) != TCL_OK) {
		Ns_Log(Warning, "nv: %s contains invalid list", path);
                goto fail;
            }
            if (largc & 1) {
		Ns_Log(Warning, "nv: %s contains odd # elements", path);
                goto fail;
            }
        }
    }

    /*
     * Create the array.
     */

    arrayPtr = ns_calloc(1, sizeof(Array));
    arrayPtr->name = Tcl_GetHashKey(&arrays, hPtr);
    arrayPtr->flags = flags;
    Tcl_InitHashTable(&arrayPtr->vars, TCL_STRING_KEYS);
    Ns_DStringInit(&arrayPtr->log);
    Ns_MutexInit(&arrayPtr->lock);
    Ns_MutexSetName2(&arrayPtr->lock, "dci:nv", arrayPtr->name);
    if (largv != NULL) {
	for (i = 0; i < largc; i += 2) {
	    SetVar(arrayPtr, largv[i], largv[i+1], 0);
	}
    }
    arrayPtr->flags &= ~NV_FLUSH;
    arrayPtr->flags |=  NV_INIT;
    if (arrayPtr->flags & NV_PERSIST) {
	if (flush.thread == NULL) {
    	    Ns_MutexInit(&flush.lock);
    	    Ns_MutexSetName(&flush.lock, "dci:nvflush");
    	    Ns_CondInit(&flush.cond);
    	    Ns_RegisterAtExit(StopFlusher, NULL);
    	    Ns_ThreadCreate(FlusherThread, NULL, 0, &flush.thread);
	}
	Ns_MutexLock(&flush.lock);
	arrayPtr->nextPtr = flush.firstPtr;
	flush.firstPtr = arrayPtr;
	Ns_MutexUnlock(&flush.lock);
    }

fail:
    if (loadbuf != NULL) {
        ns_free(loadbuf);
    }
    if (largv != NULL) {
        ckfree((char *) largv);
    }
    if (fd >= 0) {
        close(fd);
    }
    if (arrayPtr == NULL) {
    	Tcl_DeleteHashEntry(hPtr);
	return 0;
    }
    Tcl_SetHashValue(hPtr, arrayPtr);
    return 1;
}


/*
 *----------------------------------------------------------------
 *
 * GetArray --
 *
 *      Sets a pointer to the Array structure used by the
 *      NV array with the name provided in the array argument.
 *
 * Results:
 *      Returns TCL_ERROR if an NV array requested does not exist.
 *      Returns TCL_OK if th NV array requested wa found and the
 *      Array structure pointer was set.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
GetArray(Tcl_Interp *interp, char *array, Array **arrayPtrPtr)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&arrays, array);
    if (hPtr == NULL) {
    	if (interp != NULL) {
            Tcl_AppendResult(interp, "no such nv array: ", array, NULL);
	}
        return TCL_ERROR;
    }
    *arrayPtrPtr = (Array *) Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 *
 * SetVar --
 *
 *      Set (or reset) an NV array entry.
 *
 * Results:
 *      1 if a new entry was created, otherwise 0.
 *
 * Side effects;
 *      New entry is created or existing entry updated.  If
 *      cset is non-zero, only a new entry is allowed; an
 *      existing entry is not overwritten.
 *
 *----------------------------------------------------------------
 */

static int
SetVar(Array *arrayPtr, char *key, char *value, int flags)
{
    Tcl_HashEntry *hPtr;
    int new;
    size_t len;
    char *p;
    
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, key, &new);
    if (new) {
        arrayPtr->sizekeys += strlen(key);
    } else {
        if (flags & NV_CSET) {
            return new;
        } else {
            p = Tcl_GetHashValue(hPtr);
            arrayPtr->sizevalues -= strlen(p);
            ns_free(p);
        }
    }
    ++arrayPtr->nset;
    len = strlen(value);
    arrayPtr->sizevalues += len;
    p = ns_malloc(len+1);
    memcpy(p, value, len+1);
    Tcl_SetHashValue(hPtr, p);
    arrayPtr->flags |= NV_DIRTY;
    if (arrayPtr->servPtr != NULL) {
	Ns_DStringNAppend(&arrayPtr->log, "S", 1);
	Ns_DStringAppendArg(&arrayPtr->log, key);
	Ns_DStringAppendArg(&arrayPtr->log, value);
    	if (flags & NV_BROADCAST) {
	    BroadcastUpdates(arrayPtr);
	}
    }
    return new;
}


/*
 *----------------------------------------------------------------
 *
 * UnsetVar --
 *
 *      Unset an array variable.
 *
 * Results:
 *      1 if the key was found and unset, 0 if the key did not
 *      exist.
 *
 * Side effects;
 *      An entry may be removed from the array.
 *
 *----------------------------------------------------------------
 */

static int
UnsetVar(Array *arrayPtr, char *key, int flags)
{
    Tcl_HashEntry *hPtr;
    char *p;

    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, key);
    if (hPtr == NULL) {
        return 0;
    }
    p = Tcl_GetHashValue(hPtr);
    arrayPtr->sizevalues -= strlen(p);
    ns_free(p);
    arrayPtr->sizekeys -= strlen(key);
    ++arrayPtr->nunset;
    Tcl_DeleteHashEntry(hPtr);
    arrayPtr->flags |= NV_DIRTY;
    if (arrayPtr->servPtr != NULL) {
	Ns_DStringNAppend(&arrayPtr->log, "U", 1);
	Ns_DStringAppendArg(&arrayPtr->log, key);
	if (flags & NV_BROADCAST) {
	    BroadcastUpdates(arrayPtr);
	}
    }
    return 1;
}


/*
 *----------------------------------------------------------------
 *
 * FlushArray --
 *
 *      Remove all entries and re-initialize an array.
 *
 * Results:
 *      None.
 *
 * Side effects;
 *      Array is left empty.
 *
 *----------------------------------------------------------------
 */

static void
FlushArray(Array *arrayPtr, int flags)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
    while (hPtr != NULL) {
        ns_free(Tcl_GetHashValue(hPtr));
        hPtr = Tcl_NextHashEntry(&search);
    }
    ++arrayPtr->ndump;
    Tcl_DeleteHashTable(&arrayPtr->vars);
    Tcl_InitHashTable(&arrayPtr->vars, TCL_STRING_KEYS);
    arrayPtr->sizevalues = arrayPtr->sizekeys = 0;
    arrayPtr->flags |= NV_DIRTY;
    if (arrayPtr->servPtr != NULL) {
	Ns_DStringNAppend(&arrayPtr->log, "F", 1);
	if (flags & NV_BROADCAST) {
	    BroadcastUpdates(arrayPtr);
	}
    }
}


/*
 *----------------------------------------------------------------
 *
 * GetFile --
 *
 *      Returns nv file's full path & file name.
 *
 * Results:
 *      Returns nv file's full path & file name.
 *
 * Side effects;
 *      None.
 *
 *----------------------------------------------------------------
 */

static char *
GetFile(char *name, char *path)
{
    Ns_DString ds;
    
    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, dciDir, "nv", name, NULL);
    strcpy(path, ds.string);
    Ns_DStringFree(&ds);
    return path;
}


/*
 *----------------------------------------------------------------
 * AddStat, NvAddIntStat --
 *
 *     Adds a key (*name) and value (*value) to array (*var)
 * 
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *----------------------------------------------------------------
 */

static int
AddStat(Tcl_Interp *interp, char *var, char *name, char *value)
{
    if (Tcl_SetVar2(interp, var, name, value, TCL_LEAVE_ERR_MSG) == NULL) {
        return 0;
    }
    return 1;
}

static int
AddStatInt(Tcl_Interp *interp, char *var, char *name, int value)
{
    char buf[20];

    sprintf(buf, "%d", value);
    return AddStat(interp, var, name, buf);
}


/*
 *----------------------------------------------------------------
 *
 * DefunctCmd --
 *
 *      Ouputs a warning when a defunct Tcl proc is invoked.
 *
 *      Example: nv.listen
 *               nv.connect
 *
 * Results:
 *      A Warning message is outpt to the AOL Server log.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static int
DefunctCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
            argv[0], " array host port\"", NULL);
        return TCL_ERROR;
    }
    Ns_Log(Warning,"nv: defunct command: %s", argv[0]);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 *
 * RecvUpdate --
 *
 *      Read and process "over the wire" NV update commands.
 *      This routine is called when data is availabe on a
 *	client socket.  Waiting data is either read as part
 *	of the message length or the message itself.  When
 *	a complete message has arrived, the embedded commands
 *	are processed.
 *
 * Results:
 *      NS_TRUE if more data should be waited for, NS_FALSE
 *	on exit or error after socket has been closed.
 *
 * Side effects;
 *      Linked arrays may be modified.
 *
 *----------------------------------------------------------------
 */

static int
RecvUpdate(void *arg, Ns_DString *dsPtr)
{
    Tcl_HashEntry *hPtr;
    Client *clientPtr = arg;
    char *name = clientPtr->name;
    Array *arrayPtr;
    int cmd;
    char *p, *q, *netname, *key, *value;

    arrayPtr = NULL;
    p = dsPtr->string;
    q = p + dsPtr->length;
    do {
	cmd = *p++;
	if (cmd == 'A') {
	    netname = p, p += strlen(p) + 1;
	    hPtr = Tcl_FindHashEntry(&clientPtr->arrays, netname);
	    if (hPtr == NULL) {
	        Ns_Log(Warning, "nvc[%s]: unknown netname: %s", name, netname);
	    } else {
		if (arrayPtr != NULL) {
		    BroadcastUpdates(arrayPtr);
		    Ns_MutexUnlock(&arrayPtr->lock);
		}
		arrayPtr = Tcl_GetHashValue(hPtr);
		if (fDebug) {
		    Ns_Log(Notice, "nvc[%s]: array: %s = %s", name, netname, arrayPtr->name);
		}
		Ns_MutexLock(&arrayPtr->lock);
	    }
	} else if (arrayPtr == NULL) {
	    Ns_Log(Error, "nvc[%s]: recv cmd %c before array", name, cmd);
	} else {
	    if (cmd == 'F') {
		if (fDebug) {
		    Ns_Log(Notice, "nvc[%s]: flush: %s", name, arrayPtr->name);
		}
	    	FlushArray(arrayPtr, 0);
	    } else if (cmd == 'U') {
	    	key = p, p += strlen(p) + 1;
	    	UnsetVar(arrayPtr, key, 0);
		if (fDebug) {
		    Ns_Log(Notice, "nvc[%s]: unset %s[%s]", name, arrayPtr->name, key);
		}
	    } else if (cmd == 'S') {
	    	key = p, p += strlen(p) + 1;
		value = p, p += strlen(p) + 1;
	    	SetVar(arrayPtr, key, value, 0);
		if (fDebug) {
		    Ns_Log(Notice, "nvc[%s]: set %s[%s] = %s", name, arrayPtr->name, key, value);
		}
	    } else {
		/* NB: Possible garbage from nv server. */
	        Ns_Log(Error, "nvc[%s]: recv unknown cmd: %c", name, cmd);
		Ns_MutexUnlock(&arrayPtr->lock);
		arrayPtr = NULL;
	    }
	}
    } while (p < q && arrayPtr != NULL);
    if (arrayPtr == NULL) {
	return NS_ERROR;
    }
    BroadcastUpdates(arrayPtr);
    Ns_MutexUnlock(&arrayPtr->lock);

    /*
     * Wait for next message.
     */

    return NS_OK;
}


/*
 *----------------------------------------------------------------
 *
 * ClientInitMsg --
 *
 *      When a client establishes a connection, all NV data is sent
 *      to the client for synchronization. The NV data,
 *      in network command form, is either built a new and sent or the
 *      last built copy is sent (after determining that it's current).
 *
 * Results:
 *      Pointer to Dci_Msg to sync new client with.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static Dci_Msg *
ClientInitMsg(void *arg)
{
    Array *arrayPtr;
    Server *servPtr = arg;
    int flags, i, nkeys, dsize;
    size_t len;
    char *base;

    /*
     * Lock each array, clear the init required flags, and size up
     * the new init message if necessary.
     */

    flags = dsize = nkeys = 0;
    for (i = 0; servPtr->arrays[i] != NULL; ++i) {
	arrayPtr = servPtr->arrays[i];
	Ns_MutexLock(&arrayPtr->lock);
	flags |= arrayPtr->flags;
	arrayPtr->flags &= ~NV_INIT;
	dsize += arrayPtr->sizekeys + arrayPtr->sizevalues + strlen(arrayPtr->netname);
	nkeys += arrayPtr->vars.numEntries;
    }

    /*
     * If a new init message is required, allocated the required message
     * length with header and append the reload messages.
     */

    if (flags & NV_INIT) {
	len = (i * 3) + (nkeys * 3) + dsize;
	if (servPtr->init != NULL) {
	    Dci_MsgDecr(servPtr->init);
	}
	servPtr->init = Dci_MsgAlloc(len);
	base = Dci_MsgData(servPtr->init);
	for (i = 0; servPtr->arrays[i] != NULL; ++i) {
	    AppendReload(&base, servPtr->arrays[i]);
	}
    }

    /*
     * Unlock all arrays and return the current init message.
     */

    for (i = 0; servPtr->arrays[i] != NULL; ++i) {
	Ns_MutexUnlock(&servPtr->arrays[i]->lock);
    }

    return servPtr->init;
}


/*
 *----------------------------------------------------------------------
 *
 * BroadcastUpdates --
 *
 *      Broadcast update message of all queued changes. 
 *
 * Results:
 *	None.
 *     
 * Side effects:
 *      Update will appear in clients.
 *
 *----------------------------------------------------------------------
 */

static void
BroadcastUpdates(Array *arrayPtr)
{
    Dci_Msg *msgPtr;
    size_t nlen, tlen;
    char *base;

    if (arrayPtr->servPtr != NULL) {
	nlen = strlen(arrayPtr->netname) + 1;
	tlen = 1 + nlen + arrayPtr->log.length;
    	msgPtr = Dci_MsgAlloc(tlen);
    	base = Dci_MsgData(msgPtr);
	*base++ = 'A';
	memcpy(base, arrayPtr->netname, nlen);
    	memcpy(base + nlen, arrayPtr->log.string, (size_t)arrayPtr->log.length);
    	Dci_Broadcast(arrayPtr->servPtr->bcast, msgPtr);
	Ns_DStringFree(&arrayPtr->log);
    }
}


/*
 *----------------------------------------------------------------
 *
 * AppendReload --
 *
 *      Append a full flush command and then a set command
 *      for each key/value pair for a specific NV array to
 *      completely re-sync any connected clients.
 * 
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static void
AppendReload(char **basePtr, Array *arrayPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *p, *q;
    size_t n;

    p = *basePtr;
    *p++ = 'A';
    q = arrayPtr->netname;
    n = strlen(q) + 1, memcpy(p, q, n), p += n;
    *p++ = 'F';
    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
    while (hPtr != NULL) {
	*p++ = 'S';
	q = Tcl_GetHashKey(&arrayPtr->vars, hPtr);
	n = strlen(q) + 1, memcpy(p, q, n), p += n;
        q = Tcl_GetHashValue(hPtr);
	n = strlen(q) + 1, memcpy(p, q, n), p += n;
        hPtr = Tcl_NextHashEntry(&search);
    }
    *basePtr = p;
}


/*
 *----------------------------------------------------------------
 *
 * FlusherThread --
 * 
 *      Thread to flush data to file. Periodically checks every persistent
 *      NV array to determine if it was modified since last flush. If
 *      it was modified, it is written to the disk.
 *      This thread is created by:
 *      Ns_ThreadCreate(FlusherThread, NULL, 0, &flushThread);
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static void
FlusherThread(void *ignored)
{
    Array *arrayPtr;
    int dirty, fd, flags;
    Ns_Time timeout;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_DString ds;
    char file[PATH_MAX], tmp[PATH_MAX];

    Ns_ThreadSetName("-nvf-");
    if (Ns_WaitForStartup() != NS_OK) {
        return;
    }
    Tcl_DStringInit(&ds);
    Ns_Log(Notice, "waiting to flush");
    Ns_MutexLock(&flush.lock);
    do {
        /*
         * Wait if shutdown is not pending.
         */

        if (!flush.stop) {
            Ns_GetTime(&timeout);
            Ns_IncrTime(&timeout, flush.sleep, 0);
            (void) Ns_CondTimedWait(&flush.cond, &flush.lock, &timeout);
        }
	Ns_MutexUnlock(&flush.lock);

	arrayPtr = flush.firstPtr;
	while (arrayPtr != NULL) {
            dirty = 0;
            if (arrayPtr->flags & NV_PERSIST) {
        	Ns_MutexLock(&arrayPtr->lock);
		dirty = (arrayPtr->flags & NV_FLUSH );
		if (dirty) {
                    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
                    while (hPtr != NULL) {
                	Tcl_DStringAppendElement(&ds,
                        	Tcl_GetHashKey(&arrayPtr->vars, hPtr));
                	Tcl_DStringAppendElement(&ds, Tcl_GetHashValue(hPtr));
                	hPtr = Tcl_NextHashEntry(&search);
                    }
                    Tcl_DStringAppend(&ds, "\n", 1);
                    arrayPtr->flags &= ~NV_FLUSH ;
                    ++arrayPtr->nflush;
        	}
        	Ns_MutexUnlock(&arrayPtr->lock);
	    }

            /*
             * Flush to new file and rename new data.
             */

            if (dirty) {
                flags = O_CREAT|O_WRONLY|O_EXCL;
                GetFile(arrayPtr->name, file);
                sprintf(tmp, "%s.XXXXXX", file);
		if (mktemp(tmp) == NULL || tmp[0] == '\0') {
                    Ns_Log(Error, "nvf[%s]: mktemp(%s) failed: %s",
                            arrayPtr->name, file, strerror(errno));
		} else if ((fd = open(tmp, flags, 0644)) < 0) {
                    Ns_Log(Error, "nvf[%s]: open(%s) failed: %s",
                            arrayPtr->name, tmp, strerror(errno));
                } else {
                    if (write(fd, ds.string, (size_t)ds.length) != ds.length) {
                        Ns_Log(Error, "nvf[%s]: write(%s) failed: %s",
                                arrayPtr->name, tmp, strerror(errno));
		    } else if ((flags & NV_FSYNC) && fsync(fd) != 0) {
                        Ns_Log(Error, "nvf[%s]: fsync(%s) failed: %s",
                                arrayPtr->name, tmp, strerror(errno));
                    } else {
			close(fd);
			fd = -1;
			if (rename(tmp, file) != 0) {
			    Ns_Log(Error, "nvf[%s]: rename(%s, %s) failed: %s",
				    arrayPtr->name, tmp, file, strerror(errno));
			} else {
                            if (fDebug) {
			        Ns_Log(Notice, "nvf[%s]: flushed", arrayPtr->name);
                            }
			}
		    }
		    if (fd >= 0) {
			close(fd);
		    }
                }
                Tcl_DStringTrunc(&ds, 0);
            }
	    arrayPtr = arrayPtr->nextPtr;
        }
        Ns_MutexLock(&flush.lock);
    } while (!flush.stop);
    Ns_MutexUnlock(&flush.lock);
    Tcl_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------
 *
 * StopFlusher --
 *
 *      Called when AOL Server is shuting down as per the callback:
 *      Ns_RegisterAtExit(StopFlusher, NULL);
 *      Sets a flag that a shutdown i in progress to tell the flusher
 *      thread to terminate and waits for flusher to end.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------
 */

static void
StopFlusher(void *ignored)
{
    Ns_Log(Notice, "nv: waiting for flusher thread");
    Ns_MutexLock(&flush.lock);
    flush.stop = 1;
    Ns_CondSignal(&flush.cond);
    Ns_MutexUnlock(&flush.lock);
    Ns_ThreadJoin(&flush.thread, NULL);
    flush.thread = NULL;
    Ns_Log(Notice, "nv: flusher thread exited");
}
