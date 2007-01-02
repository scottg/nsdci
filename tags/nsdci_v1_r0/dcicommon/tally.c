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

#define NT_MAXTALLIES 1000
#define NT_MAXBUCKET 15
#define NT_MAXKEY    31
#define NT_CMD	     0x6274  /* 'nt' */

/*
 * Define to 1 for compatibility with old nts.so module.
 */

typedef struct Req {
    uint32_t value;
    char bucket[NT_MAXBUCKET+1];
    char key[NT_MAXKEY+1];
} Req;

/*
 * The following structure defines a message waiting to
 * be sent to the tally server.
 *
 */

typedef struct Msg {
    struct Msg *nextPtr;
    struct Req req;
} Msg;

typedef struct {
    unsigned long count;
    unsigned long total;
} Sum;

typedef struct {
    char key[NT_MAXKEY+1];
    Sum  sum;
} Tally;

typedef struct {
    Ns_Thread thread;
    Dci_Rpc *rpc;
    int shutdown;
    Ns_Mutex lock;
    Ns_Cond  cond;
    struct {
	Msg *first;
	Msg *last;
	Msg *free;
    } msg;
} Client;

typedef struct {
    char *backupFile;
    Tcl_HashTable buckets;
    Ns_Mutex lock;
} Server;

static Ns_Callback NtsAtExit;
static int NtsWriteFile(Server *, Tcl_Interp *, char *file);
static int NtsReadFile(Server *, Tcl_Interp *, char *file);
static Tcl_CmdProc NtsGetCmd;
static Tcl_CmdProc NtsPeekCmd;
static Tcl_CmdProc NtsReadCmd;
static Tcl_CmdProc NtsWriteCmd;
static Tcl_CmdProc NtsDumpCmd;
static Tcl_CmdProc NtsExistsCmd;
static Ns_TclInterpInitProc AddServerCmds;
static Dci_RpcProc NtsProc;
static Dci_ListenExitProc NtcExit;
static Ns_ThreadProc NtcThread;
static Tcl_CmdProc NtcSendCmd;
static Ns_TclInterpInitProc AddClientCmds;
static int fDebug;


int
DciNtInit(char *server, char *module)
{
    Server *sPtr;
    Client *cPtr;
    Msg *msgPtr;
    char *path;
    int timeout, max;
    Ns_Set *set;

    Dci_LogIdent(module, rcsid);

    path = Ns_ConfigGetPath(server, module, "nt", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
        fDebug = 0;
    }

    /*
     * Enable the tally client.
     */

    path = Ns_ConfigGetPath(server, module, "nt/client", NULL);
    if (path != NULL) {
	cPtr = ns_calloc(1, sizeof(Client));
	if (!Ns_ConfigGetInt(path, "timeout", &timeout)) {
    	    timeout = 5;
	}
	cPtr->rpc = Dci_RpcCreateClient(server, module, "nt", timeout);
	if (cPtr->rpc == NULL) {
	    return NS_ERROR;
	}
	if (!Ns_ConfigGetInt(path, "max", &max)) {
    	    max = NT_MAXTALLIES;
	}
	msgPtr = ns_malloc(sizeof(Msg) * max);
	while (max-- > 0) {
	    msgPtr->nextPtr = cPtr->msg.free;
	    cPtr->msg.free = msgPtr;
	    ++msgPtr;
	}
	Ns_ThreadCreate(NtcThread, cPtr, 0, &cPtr->thread);
	Ns_RegisterAtShutdown(NtcExit, cPtr);
    	Ns_TclInitInterps(server, AddClientCmds, cPtr);
    }

    /*
     * Start the tally server.
     */

    path = Ns_ConfigGetPath(server, module, "nt/server/clients", NULL);
    set = Ns_ConfigGetSection(path);
    if (set != NULL && Ns_SetSize(set) > 0) {
	sPtr = ns_calloc(1, sizeof(Server));
	Tcl_InitHashTable(&sPtr->buckets, TCL_STRING_KEYS);
    	path = Ns_ConfigGetPath(server, module, "nt/server", NULL);
    	if (Dci_RpcCreateServer(server, module, "nt", NULL, set, NtsProc,
				sPtr) != NS_OK) {
	    return NS_ERROR;
	}
    	sPtr->backupFile = Ns_ConfigGetValue(path, "backupfile");
    	if (sPtr->backupFile != NULL) {
            Ns_RegisterAtExit(NtsAtExit, sPtr);
    	}
    	Ns_TclInitInterps(server, AddServerCmds, sPtr);
	if (sPtr->backupFile != NULL) {
	    if (NtsReadFile(sPtr, NULL, sPtr->backupFile) != TCL_OK) {
		Ns_Log(Error, "nts: could not read: %s", sPtr->backupFile);
	    } else {
		Ns_Log(Notice, "nts: restored from: %s", sPtr->backupFile);
		if (unlink(sPtr->backupFile) != 0) {
	    	    Ns_Log(Error, "nts: unlink(%s) failed: %s", sPtr->backupFile,
			    strerror(errno));
		}
	    }
	}
    }
    return NS_OK;
}


static int
AddClientCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nt.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nt.send", NtcSendCmd, arg, NULL);
    return TCL_OK;
}


static int
AddServerCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nt.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nt.get", NtsGetCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nt.peek", NtsPeekCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nt.dump", NtsDumpCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nt.exists", NtsExistsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nt.read", NtsReadCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nt.write", NtsWriteCmd, arg, NULL);
    return TCL_OK;
}


static void
NtcExit(void *arg)
{
    Client *cPtr = arg;

    Ns_Log(Notice, "nt: shutdown pending");
    Ns_MutexLock(&cPtr->lock);
    cPtr->shutdown = 1;
    Ns_CondBroadcast(&cPtr->cond);
    Ns_MutexUnlock(&cPtr->lock);
    Ns_ThreadJoin(&cPtr->thread, NULL);
    Ns_Log(Notice, "nt: shutdown complete");
}


static void
NtcThread(void *arg)
{
    Msg *msgPtr, *firstPtr, *freePtr;
    Client *cPtr = arg;
    Ns_DString ds;

    Ns_ThreadSetName("-ntc-");
    Ns_Log(Notice, "starting");
    Ns_DStringInit(&ds);
    freePtr = NULL;
    Ns_MutexLock(&cPtr->lock);
    while (!cPtr->shutdown) {
        /*
         * Wait for messages on the queue.
         */

        while (cPtr->msg.first == NULL && !cPtr->shutdown) {
            Ns_CondWait(&cPtr->cond, &cPtr->lock);
        }

	/*
	 * Grab the waiting list.
	 */

	firstPtr = cPtr->msg.first;
	cPtr->msg.first = cPtr->msg.last = NULL;
        Ns_MutexUnlock(&cPtr->lock);

	/*
	 * Copy the tallies to the RPC message and move to
	 * pending free list.
	 */

	while ((msgPtr = firstPtr) != NULL) {
	    Ns_DStringNAppend(&ds, (char *) &msgPtr->req, sizeof(Req));
	    firstPtr = msgPtr->nextPtr;
	    msgPtr->nextPtr = freePtr;
	    freePtr = msgPtr;
	}

	/*
	 * Attempt to flush the tallies via RPC.
	 */

	if (ds.length > 0 && Dci_RpcSend(cPtr->rpc, NT_CMD, &ds, NULL) == RPC_OK) {
	    Ns_DStringFree(&ds);
	}
        Ns_MutexLock(&cPtr->lock);

	/*
	 * If the RPC worked, return the message buffers to the
	 * free list.
	 */

	if (ds.length == 0) {
	    while ((msgPtr = freePtr) != NULL) {
		freePtr = msgPtr->nextPtr;
		msgPtr->nextPtr = cPtr->msg.free;
		cPtr->msg.free = msgPtr;
	    }
	}
    }

    Ns_MutexUnlock(&cPtr->lock);
    Ns_Log(Notice, "stopping");
    Ns_DStringFree(&ds);
}


static int
NtcSendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Client *cPtr = arg;
    Msg *msgPtr;
    int value;

    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " bucket key value\"", NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[1]) >= NT_MAXBUCKET) {
        Tcl_AppendResult(interp, "invalid bucket: ", argv[1], NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[2]) >= NT_MAXKEY) {
        Tcl_AppendResult(interp, "invalid key: ", argv[2], NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &value) != TCL_OK) {
        return TCL_ERROR;
    }
    msgPtr = NULL;
    Ns_MutexLock(&cPtr->lock);
    if (cPtr->msg.free != NULL) {
        msgPtr = cPtr->msg.free;
        cPtr->msg.free = msgPtr->nextPtr;
        strcpy(msgPtr->req.bucket, argv[1]);
        strcpy(msgPtr->req.key, argv[2]);
        msgPtr->req.value = htonl(value);
	msgPtr->nextPtr = NULL;
	if (cPtr->msg.first == NULL) {
	    cPtr->msg.first = msgPtr;
	    Ns_CondBroadcast(&cPtr->cond);
	} else {
	    cPtr->msg.last->nextPtr = msgPtr;
	}
	cPtr->msg.last = msgPtr;
    }
    Ns_MutexUnlock(&cPtr->lock);
    if (msgPtr == NULL || fDebug) {
        Ns_Log(msgPtr ? Notice : Warning, "nt: add %s/%s = %d: %s", argv[1], argv[2], value, msgPtr ? "sent" : "dropped");
    }
    return TCL_OK;
}


static Sum *
GetSum(Server *sPtr, char *bucket, char *key)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Sum *sumPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&sPtr->buckets, bucket, &new);
    tablePtr = Tcl_GetHashValue(hPtr);
    if (tablePtr == NULL) {
	tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
	Tcl_SetHashValue(hPtr, tablePtr);
    }
    hPtr = Tcl_CreateHashEntry(tablePtr, key, &new);
    if (!new) {
	sumPtr = Tcl_GetHashValue(hPtr);
    } else {
	sumPtr = ns_malloc(sizeof(Sum));
	sumPtr->count = sumPtr->total = 0;
	Tcl_SetHashValue(hPtr, sumPtr);
    }
    return sumPtr;
}


static int
NtsProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    Server *sPtr = arg;
    Req *reqPtr;
    Sum *sumPtr;
    uint32_t value;
    int n;

    if (cmd != NT_CMD || (inPtr->length % sizeof(Req)) != 0) {
    	Ns_Log(Error, "invalid request");
    	return NS_ERROR;
    }
    n = inPtr->length / sizeof(Req);
    reqPtr = (Req *) inPtr->string;
    while (n-- > 0) {
	value = ntohl(reqPtr->value);
    	Ns_MutexLock(&sPtr->lock);
	sumPtr = GetSum(sPtr, reqPtr->bucket, reqPtr->key);
    	++sumPtr->count;
    	sumPtr->total += value;
    	Ns_MutexUnlock(&sPtr->lock);
    	if (fDebug) {
	    Ns_Log(Notice, "nts: vote %s/%s = %d", reqPtr->bucket, reqPtr->key, value);
    	}
	++reqPtr;
    }
    return NS_OK;
}


static void
NtsAtExit(void *arg)
{
    Server *sPtr = arg;

    if (NtsWriteFile(sPtr, NULL, sPtr->backupFile) != TCL_OK) {
	Ns_Log(Warning, "could not write backup file: %s", sPtr->backupFile);
    } else {
	Ns_Log(Notice, "nts: saved: %s", sPtr->backupFile);
    }
}


static int
NtsWriteFile(Server *sPtr, Tcl_Interp *interp, char *file)
{
    Sum *sumPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    Tcl_HashSearch bSearch, tSearch;
    Tally tally;
    Tcl_Channel chan;
    char key[NT_MAXKEY+1];
    int n;

    chan = Tcl_OpenFileChannel(interp, file, "w", 0644);
    if (chan == NULL) {
        return TCL_ERROR;
    }

    Ns_MutexLock(&sPtr->lock);
    hPtr = Tcl_FirstHashEntry(&sPtr->buckets, &bSearch);
    while (hPtr != NULL) {
        tablePtr = Tcl_GetHashValue(hPtr);
        if (tablePtr != NULL) {
	    n = tablePtr->numEntries;
            if (n > 0) {
	        strcpy(key, Tcl_GetHashKey(&sPtr->buckets, hPtr));
                Tcl_Write(chan, key, sizeof(key));
                Tcl_Write(chan, (char *) &n, sizeof(n));
                hPtr = Tcl_FirstHashEntry(tablePtr, &tSearch);
                while (hPtr != NULL) {
                    strcpy(tally.key, Tcl_GetHashKey(tablePtr, hPtr));
                    sumPtr = (Sum *) Tcl_GetHashValue(hPtr);
                    tally.sum.count = sumPtr->count;
                    tally.sum.total = sumPtr->total;
                    Tcl_Write(chan, (char *) &tally, sizeof(Tally));
	            hPtr = Tcl_NextHashEntry(&tSearch);
	        }
            }
        }
        hPtr = Tcl_NextHashEntry(&bSearch);
    }
    Ns_MutexUnlock(&sPtr->lock);
    Tcl_Close(interp, chan);
    return TCL_OK;
}


static int
Read(Tcl_Channel chan, void *ptr, int size)
{
    if (Tcl_Read(chan, (char *) ptr, size) != size) {
	return 0;
    }
    return 1;
}


static int
NtsReadFile(Server *sPtr, Tcl_Interp *interp, char *file)
{
    Sum *sumPtr;
    Tally tally;
    Tcl_Channel chan;
    char bucket[NT_MAXBUCKET+1];
    int n;

    chan = Tcl_OpenFileChannel(interp, file, "r", 0644);
    if (chan == NULL) {
        return TCL_ERROR;
    }

    Ns_MutexLock(&sPtr->lock);
    while (Read(chan, bucket, sizeof(bucket))
	    && Read(chan, &n, sizeof(n))) {
	while (--n >= 0 && Read(chan, &tally, sizeof(tally))) {
	    sumPtr = GetSum(sPtr, bucket, tally.key);
	    sumPtr->total = tally.sum.total;
	    sumPtr->count = tally.sum.count;
	}
    }
    Ns_MutexUnlock(&sPtr->lock);
    Tcl_Close(interp, chan);
    return TCL_OK;
}


static int
NtsReadCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Server *sPtr = arg;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }
    return NtsReadFile(sPtr, interp, argv[1]);
}


static int
NtsWriteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Server *sPtr = arg;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }
    return NtsWriteFile(sPtr, interp, argv[1]);
}


static int
GetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int cmd)
{
    Server *sPtr = arg;
    Sum *sumPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    Tcl_HashSearch search;
    Tcl_DString ds;
    char buf[20];

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " bucket proc\"", NULL);
	return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    tablePtr = NULL;
    Ns_MutexLock(&sPtr->lock);
    hPtr = Tcl_FindHashEntry(&sPtr->buckets, argv[1]);
    if (hPtr != NULL) {
	tablePtr = Tcl_GetHashValue(hPtr);
	if (cmd == 'g') {
	    Tcl_SetHashValue(hPtr, NULL);
	}
    }
    if (cmd == 'g') {
    	Ns_MutexUnlock(&sPtr->lock);
    }
    if (tablePtr != NULL) {
	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	while (hPtr != NULL) {
	    sumPtr = Tcl_GetHashValue(hPtr);
	    Tcl_DStringAppendElement(&ds, argv[2]);
	    Tcl_DStringAppendElement(&ds, Tcl_GetHashKey(tablePtr, hPtr));
	    sprintf(buf, "%ld", sumPtr->count);
	    Tcl_DStringAppendElement(&ds, buf);
	    sprintf(buf, "%ld", sumPtr->total);
	    Tcl_DStringAppendElement(&ds, buf);
	    if (Tcl_Eval(interp, ds.string) != TCL_OK) {
		Ns_TclLogError(interp);
	    }
	    Tcl_DStringTrunc(&ds, 0);
	    if (cmd == 'g') {
	    	ns_free(sumPtr);
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
	if (cmd == 'g') {
	    Tcl_DeleteHashTable(tablePtr);
	    ns_free(tablePtr);
	}
    }
    if (cmd != 'g') {
	Ns_MutexUnlock(&sPtr->lock);
    }

    Tcl_DStringFree(&ds);
    return TCL_OK;
}


static int
NtsGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return GetCmd(arg, interp, argc, argv, 'g');
}


static int
NtsPeekCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return GetCmd(arg, interp, argc, argv, 'p');
}


static int
NtsExistsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Server *sPtr = arg;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " bucket\"", NULL);
	return TCL_ERROR;
    }

    tablePtr = NULL;
    Ns_MutexLock(&sPtr->lock);
    hPtr = Tcl_FindHashEntry(&sPtr->buckets, argv[1]);
    if (hPtr != NULL) {
	tablePtr = Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&sPtr->lock);
    Tcl_SetResult(interp, tablePtr ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


static int
NtsDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Server *sPtr = arg;
    Sum *sumPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    Tcl_HashSearch search;
    Tcl_DString ds;
    char buf[20];

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " bucket\"", NULL);
	return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    tablePtr = NULL;
    Ns_MutexLock(&sPtr->lock);
    hPtr = Tcl_FindHashEntry(&sPtr->buckets, argv[1]);
    if (hPtr != NULL && (tablePtr = Tcl_GetHashValue(hPtr)) != NULL) {
	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	while (hPtr != NULL) {
	    sumPtr = Tcl_GetHashValue(hPtr);
	    Tcl_DStringStartSublist(&ds);
	    Tcl_DStringAppendElement(&ds, Tcl_GetHashKey(tablePtr, hPtr));
	    sprintf(buf, "%ld", sumPtr->count);
	    Tcl_DStringAppendElement(&ds, buf);
	    sprintf(buf, "%ld", sumPtr->total);
	    Tcl_DStringAppendElement(&ds, buf);
	    Tcl_DStringEndSublist(&ds);
	    hPtr = Tcl_NextHashEntry(&search);
	}
    }
    Ns_MutexUnlock(&sPtr->lock);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}
