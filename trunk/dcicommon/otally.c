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
#define NT_MAXNAME 32

/*
 * Define to 1 for compatibility with old nts.so module.
 */

#define NTS_COMPAT 0

typedef struct Req {
#if ! NTS_COMPAT
    uint32_t value;
#endif
    char bucket[NT_MAXNAME+1];
    char key[NT_MAXNAME+1];
#if NTS_COMPAT
    int value;
#endif
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
    char key[NT_MAXNAME+1];
    Sum  sum;
} Tally;

static int fDebug;

static Dci_ListenAcceptProc NtcAccept;
static Dci_ListenExitProc NtcExit;
static Ns_ThreadProc NtcThread;
static Tcl_CmdProc NtcSendCmd;
static Ns_TclInterpInitProc AddClientCmds;
static Msg *lastMsgPtr;
static Msg *firstMsgPtr;
static Msg *firstFreeMsgPtr;
static int shutdownPending;
static int nThreads;
static Ns_Thread lastThread;
static Ns_Mutex clientLock;
static Ns_Cond clientCond;

static Dci_ServerProc NtsProc;
static Ns_Callback NtsAtExit;
static int NtsWriteFile(Tcl_Interp *, char *file);
static int NtsReadFile(Tcl_Interp *, char *file);
static Tcl_CmdProc NtsGetCmd;
static Tcl_CmdProc NtsIOCmd;
static Tcl_CmdProc NtsDumpCmd;
static Tcl_CmdProc NtsExistsCmd;
static Ns_TclInterpInitProc AddServerCmds;
static char *backupFile;
static Tcl_HashTable buckets;
static Ns_Mutex serverLock;
static int nDelay;
static int nRepeater;


int
DciNtInit(char *server, char *module)
{
    Msg *msgPtr;
    char *path, *addr;
    int port, max;
    Ns_Set *set;

    Dci_LogIdent(module, rcsid);

    path = Ns_ConfigGetPath(server, module, "nt", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
        fDebug = 0;
    }
    if (!Ns_ConfigGetBool(path, "delay", &nDelay)) {
        nDelay = 1;
    }
    if (!Ns_ConfigGetBool(path, "repeater", &nRepeater)) {
        nRepeater = 0;
    }
    Ns_MutexSetName(&clientLock, "dci:ntc");
    Ns_MutexSetName(&serverLock, "dci:nts");

    /*
     * Enable the tally client.
     */

    path = Ns_ConfigGetPath(server, module, "nt/client", NULL);
    if (path != NULL) {
	addr = Ns_ConfigGet(path, "address");
	if (!Ns_ConfigGetInt(path, "port", &port)) {
            Ns_Log(Warning, "nt: required port parameter missing");
	    return NS_ERROR;
	}
	if (!Ns_ConfigGetInt(path, "max", &max) || max < 1) {
            max = NT_MAXTALLIES;
	}
        while (--max) {
            msgPtr = ns_malloc(sizeof(Msg));
            msgPtr->nextPtr = firstFreeMsgPtr;
            firstFreeMsgPtr = msgPtr;
	}
	if (Dci_ListenCallback("nt", addr, port, NtcAccept, NtcExit, NULL) != NS_OK) {
	    return NS_ERROR;
	}
    	Ns_TclInitInterps(server, AddClientCmds, NULL);
    }

    /*
     * Start the tally server.
     */

    path = Ns_ConfigGetPath(server, module, "nt/server/clients", NULL);
    if (path != NULL) {
    	Tcl_InitHashTable(&buckets, TCL_STRING_KEYS);
    	set = Ns_ConfigGetSection(path);
    	if (Dci_CreateServer("nts", NULL, set, NtsProc) != NS_OK) {
	    Ns_Log(Error, "nts: no valid clients defined");
	    return NS_ERROR;
	}
    	path = Ns_ConfigGetPath(server, module, "nt/server", NULL);
    	backupFile = Ns_ConfigGetValue(path, "backupfile");
    	if (backupFile != NULL) {
            Ns_RegisterAtExit(NtsAtExit, NULL);
            if (nRepeater == 0) {
                if (NtsReadFile(NULL, backupFile) != TCL_OK) {
                    Ns_Log(Error, "nts: Unable to open %s: %s", backupFile,
                           strerror(Tcl_GetErrno()) );
                } else {
                    Ns_Log(Notice, "nts: restored from: %s", backupFile);
                    if (unlink(backupFile) != 0) {
                        Ns_Log(Error, "nts: unlink(%s) failed: %s", backupFile,
                               strerror(errno));
                    }
                }
            }
    	}
        /*
         * Add server commands if im not a repeater
         */
        if (nRepeater == 0 ) {
            Ns_TclInitInterps(server, AddServerCmds, NULL);
        }
    }

    return NS_OK;
}


static int
AddClientCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "nt.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nt.send", NtcSendCmd, NULL, NULL);
    return TCL_OK;
}


static int
AddServerCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "nt.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nt.get", NtsGetCmd, (ClientData) 'g', NULL);
    Tcl_CreateCommand(interp, "nt.peek", NtsGetCmd, (ClientData) 'p', NULL);
    Tcl_CreateCommand(interp, "nt.dump", NtsDumpCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nt.exists", NtsExistsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nt.read", NtsIOCmd, (ClientData) 'r', NULL);
    Tcl_CreateCommand(interp, "nt.write", NtsIOCmd, (ClientData) 'w', NULL);
    return TCL_OK;
}


static void
NtcExit(void *arg)
{
    Ns_Thread joinThread;

    Ns_Log(Notice, "nt: shutdown pending");
    Ns_MutexLock(&clientLock);
    shutdownPending = 1;
    Ns_CondBroadcast(&clientCond);
    while (nThreads > 0) {
        Ns_CondWait(&clientCond, &clientLock);
    }
    joinThread = lastThread;
    lastThread = NULL;
    Ns_MutexUnlock(&clientLock);
    if (joinThread != NULL) {
        Ns_ThreadJoin(&joinThread, NULL);
    }
    Ns_Log(Notice, "nt: shutdown complete");
}


static void
NtcAccept(SOCKET sock, void *arg)
{
    Ns_Thread thread;

    Dci_SockOpts(sock, DCI_SOCK_CLIENT);
    Ns_SockSetNonBlocking(sock);
    Ns_ThreadCreate(NtcThread, (void *) sock, 0, &thread);
    Ns_MutexLock(&clientLock);
    ++nThreads;
    Ns_CondBroadcast(&clientCond);
    Ns_MutexUnlock(&clientLock);
}


static void
QueueMsg(Msg *msgPtr)
{
    msgPtr->nextPtr = NULL;
    if (firstMsgPtr == NULL) {
        firstMsgPtr = msgPtr;
        Ns_CondBroadcast(&clientCond);
    } else {
        lastMsgPtr->nextPtr = msgPtr;
    }
    lastMsgPtr = msgPtr;
}


static int
SendMsg(SOCKET sock, Req *reqPtr)
{
    int n, len;
    char *ptr;

    len = sizeof(Req);
    ptr = (char *) reqPtr;
    while (len > 0) {
	n = Ns_SockSend(sock, ptr, len, 1);
	if (n < 0) {
	    Ns_Log(Warning, "send() failed: %s", strerror(errno));
	    return 0;
	}
	ptr += n;
	len -= n;
    }
    return 1;
}


static void
NtcThread(void *arg)
{
    Msg *msgPtr, *firstPtr, *freePtr;
    int sock, ok;
    Ns_Thread joinThread;

    sock = (SOCKET) arg;
    Ns_ThreadSetName("-ntc-");
    Ns_Log(Notice, "starting");
    ok = 1;
    Ns_MutexLock(&clientLock);
    while (!shutdownPending && ok) {

        /*
         * Sleep a bit to allow messages to accumulate.
         */

	if (nDelay > 0) {
	    Ns_MutexUnlock(&clientLock);
	    sleep((unsigned long)nDelay);
	    Ns_MutexLock(&clientLock);
	}

        /*
         * Wait for messages on the queue.
         */

        while (firstMsgPtr == NULL && !shutdownPending) {
            Ns_CondWait(&clientCond, &clientLock);
        }
	firstPtr = firstMsgPtr;
	firstMsgPtr = lastMsgPtr = NULL;
        Ns_MutexUnlock(&clientLock);

        /*
         * Send the tally messages as long as the connection
	 * is valid.  Messages which are sent are pushed
	 * on the local free list.
         */

	freePtr = NULL;
	while (ok && (msgPtr = firstPtr) != NULL) {
            ok = SendMsg(sock, &msgPtr->req);
	    if (ok) {
		firstPtr = msgPtr->nextPtr;
		msgPtr->nextPtr = freePtr;
		freePtr = msgPtr;
	    }
	    if (fDebug) {
    	        Ns_Log(ok ? Notice : Warning, "nt: send %s/%s = %d: %s",
			msgPtr->req.bucket, msgPtr->req.key,
			ntohl((uint32_t) msgPtr->req.value), ok ? "sent" : "requeued");
	    }
	}

	/*
	 * Return all sent messages on the local free list
	 * to the shared free list.
	 */

        Ns_MutexLock(&clientLock);
	while ((msgPtr = freePtr) != NULL) {
	    freePtr = msgPtr->nextPtr;
	    msgPtr->nextPtr = firstFreeMsgPtr;
	    firstFreeMsgPtr = msgPtr;
	}

	/*
	 * Push any messages which could not be sent
	 * back on the queue.
	 */

    	if (firstPtr != NULL) {
	    while ((msgPtr = firstPtr) != NULL) {
		firstPtr = msgPtr->nextPtr;
		QueueMsg(msgPtr);
            }
	    Ns_CondBroadcast(&clientCond);
	}
    }

    --nThreads;
    if (firstMsgPtr != NULL || nThreads == 0) {
	Ns_CondBroadcast(&clientCond);
    }
    joinThread = lastThread;
    Ns_ThreadSelf(&lastThread);
    Ns_MutexUnlock(&clientLock);
    if (joinThread != NULL) {
	Ns_ThreadJoin(&joinThread, NULL);
    }
    Ns_Log(Notice, "nt: dropped");
    close(sock);
}


static int
NtcSendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Msg *msgPtr;
    int value;

    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " bucket key value\"", NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[1]) >= NT_MAXNAME) {
        Tcl_AppendResult(interp, "invalid bucket: ", argv[1], NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[2]) >= NT_MAXNAME) {
        Tcl_AppendResult(interp, "invalid key: ", argv[2], NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &value) != TCL_OK) {
        return TCL_ERROR;
    }

    msgPtr = NULL;

    Ns_MutexLock(&clientLock);
    if (firstFreeMsgPtr != NULL) {
        msgPtr = firstFreeMsgPtr;
        firstFreeMsgPtr = msgPtr->nextPtr;

        strcpy(msgPtr->req.bucket, argv[1]);
        strcpy(msgPtr->req.key, argv[2]);
        msgPtr->req.value = htonl((uint32_t) value);
	QueueMsg(msgPtr);

    }
    Ns_MutexUnlock(&clientLock);

    if (msgPtr == NULL || fDebug) {
        Ns_Log(msgPtr ? Notice : Warning, "nt: add %s/%s = %d: %s", argv[1], argv[2], value, msgPtr ? "sent" : "dropped");
    }

    return TCL_OK;
}


static Sum *
GetSum(char *bucket, char *key)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Sum *sumPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&buckets, bucket, &new);
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
NtsProc(Dci_Client *client, void *ignored, int why)
{
    Msg *msgPtr;
    Req *reqPtr;
    Sum *sumPtr;
    uint32_t value;

    reqPtr = Dci_ServerGetData(client);
    if (why == DCI_SERVER_DROP) {
	if (reqPtr != NULL) {
	    ns_free(reqPtr);
	    Dci_ServerSetData(client, NULL);
	}
	return NS_OK;
    }

    if (reqPtr == NULL) {
	reqPtr = ns_malloc(sizeof(Req));
	Dci_ServerSetData(client, reqPtr);
    }
    if (why == DCI_SERVER_RECV) {
        if (nRepeater == 1) {
            /*
             * If I'm a repeater, queue the message to be forwarded
             */
            msgPtr = NULL;

            Ns_MutexLock(&clientLock);
            if (firstFreeMsgPtr != NULL) {
                msgPtr = firstFreeMsgPtr;
                firstFreeMsgPtr = msgPtr->nextPtr;
        
                strcpy(msgPtr->req.bucket, reqPtr->bucket);
                strcpy(msgPtr->req.key, reqPtr->key);
                msgPtr->req.value = reqPtr->value;
        	QueueMsg(msgPtr);

            }
            Ns_MutexUnlock(&clientLock);

            if (msgPtr == NULL || fDebug) {
                Ns_Log(msgPtr ? Notice : Warning, "nt: add %s/%s = %d: %s", reqPtr->bucket, reqPtr->key, reqPtr->value, msgPtr ? "repeated" : "dropped");
            }

        } else {
            value = ntohl(reqPtr->value);
            Ns_MutexLock(&serverLock);
            sumPtr = GetSum(reqPtr->bucket, reqPtr->key);
            ++sumPtr->count;
            sumPtr->total += value;
            Ns_MutexUnlock(&serverLock);
            if (fDebug) {
                Ns_Log(Notice, "nts: vote %s/%s = %d", reqPtr->bucket, reqPtr->key, value);
    	    }
        }
    }

    Dci_ServerRecv(client, reqPtr, sizeof(Req));
    return NS_OK;
}


static void
NtsAtExit(void *arg)
{
    if (NtsWriteFile(NULL, backupFile) != TCL_OK) {
	Ns_Log(Warning, "could not write backup file: %s", backupFile);
    } else {
	Ns_Log(Notice, "nts: saved: %s", backupFile);
    }
}


static int
NtsWriteFile(Tcl_Interp *interp, char *file)
{
    Sum *sumPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    Tcl_HashSearch bSearch, tSearch;
    Tally tally;
    Tcl_Channel chan;
    char key[NT_MAXNAME+1];
    int n;

    chan = Tcl_OpenFileChannel(interp, file, "w", 0644);
    if (chan == NULL) {
        return TCL_ERROR;
    }

    Ns_MutexLock(&serverLock);
    hPtr = Tcl_FirstHashEntry(&buckets, &bSearch);
    while (hPtr != NULL) {
        tablePtr = Tcl_GetHashValue(hPtr);
        if( tablePtr != NULL ) {
            n = tablePtr->numEntries;
            if (n > 0) {
                strcpy(key, Tcl_GetHashKey(&buckets, hPtr));
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
    Ns_MutexUnlock(&serverLock);
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
NtsReadFile(Tcl_Interp *interp, char *file)
{
    Sum *sumPtr;
    Tally tally;
    Tcl_Channel chan;
    char bucket[NT_MAXNAME+1];
    int n;

    chan = Tcl_OpenFileChannel(interp, file, "r", 0644);
    if (chan == NULL) {
        return TCL_ERROR;
    }

    Ns_MutexLock(&serverLock);
    while (Read(chan, bucket, sizeof(bucket))
	    && Read(chan, &n, sizeof(n))) {
	while (--n >= 0 && Read(chan, &tally, sizeof(tally))) {
	    sumPtr = GetSum(bucket, tally.key);
	    sumPtr->total = tally.sum.total;
	    sumPtr->count = tally.sum.count;
	}
    }
    Ns_MutexUnlock(&serverLock);
    Tcl_Close(interp, chan);
    return TCL_OK;
}


static int
NtsIOCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int cmd = (int) arg;
    int status;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }
    if (cmd == 'w') {
	status = NtsWriteFile(interp, argv[1]);
    } else {
	status = NtsReadFile(interp, argv[1]);
    }
    return status;
}


static int
NtsGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Sum *sumPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;
    Tcl_HashSearch search;
    Tcl_DString ds;
    char buf[20];
    int cmd = (int) arg;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " bucket proc\"", NULL);
	return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    tablePtr = NULL;
    Ns_MutexLock(&serverLock);
    hPtr = Tcl_FindHashEntry(&buckets, argv[1]);
    if (hPtr != NULL) {
	tablePtr = Tcl_GetHashValue(hPtr);
	if (cmd == 'g') {
	    Tcl_SetHashValue(hPtr, NULL);
	}
    }
    if (cmd == 'g') {
    	Ns_MutexUnlock(&serverLock);
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
	Ns_MutexUnlock(&serverLock);
    }

    Tcl_DStringFree(&ds);
    return TCL_OK;
}


static int
NtsExistsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashTable *tablePtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " bucket\"", NULL);
	return TCL_ERROR;
    }

    tablePtr = NULL;
    Ns_MutexLock(&serverLock);
    hPtr = Tcl_FindHashEntry(&buckets, argv[1]);
    if (hPtr != NULL) {
	tablePtr = Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&serverLock);
    Tcl_SetResult(interp, tablePtr ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


static int
NtsDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
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
    Ns_MutexLock(&serverLock);
    hPtr = Tcl_FindHashEntry(&buckets, argv[1]);
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
    Ns_MutexUnlock(&serverLock);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}
