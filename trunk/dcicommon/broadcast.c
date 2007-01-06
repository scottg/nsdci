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

/*
 * The following structure is used to queue a message to be sent.
 */
 
typedef struct Msg {
    struct Msg *nextPtr;
    int refcnt;
    int size;
    uint32_t hdr;
    /* Message data will follow after length header. */
} Msg;

/*
 * The following structure maintains the context for a client to
 * receive messages.
 */
 
#define BCAST_READY	0
#define BCAST_CONNECT	1
#define BCAST_LOGIN	2

typedef struct Client {
    struct Client *nextPtr;
    char *name;
    int sock;
    int port;
    int state;
    struct pollfd *pfd;
    time_t nextwarn;
    Msg *msgPtr;
    char *bufPtr;
    int nsend;
    int timeout;
    time_t qtime;
    char host[4];
} Client;

/*
 * The following structure defines a configured broadcaster.  An
 * opaque pointer to this structure is used as the handle when
 * sending broadcasts.
 */
 
typedef struct Broadcast {
    struct Broadcast *nextPtr;
    void   *clientData;
    Dci_MsgProc *initProc;
    Client *firstClientPtr, *firstActivePtr, *firstRetryPtr;
    Msg *firstMsgPtr;
    Msg *lastMsgPtr;
    int triggerPipe[2];
    Ns_Thread thread;
    Ns_Mutex lock;
    char   name[4];
} Broadcast;

static Broadcast *firstBroadcastPtr;
static void RetryClient(Client *cPtr, Client **firstRetryPtrPtr);
static void TriggerBroadcaster(Broadcast *bPtr);
static void MsgSend(Client *cPtr, Msg *msgPtr);
static void MsgDone(Client *cPtr);
static Ns_TclInterpInitProc AddCmds;
static Ns_ThreadProc DciBroadcaster;
static int warnInterval;
static int retryInterval;
static int timeoutInterval;
static int stimeoutInterval;
static int fDebug;
static int shutdownPending;
static Ns_Mutex lock;
static Ns_Callback StopBroadcasters;
static int StatsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int ClientsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int DumpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int NamesObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);



/*
 *----------------------------------------------------------------------
 *
 * DciBroadcastInit --
 *
 *	Initialize the broadcaster.
 *
 * Results:
 *	NS_OK.
 *
 * Side effects:
 *	Shutdown procedure registered.
 *
 *----------------------------------------------------------------------
 */

int
DciBroadcastInit(char *server, char *module)
{
    char *path;

    Dci_LogIdent(module, rcsid);
    path = Ns_ConfigGetPath(server, module, "broadcast", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
    	fDebug = 0;
    }
    Ns_MutexInit(&lock);
    Ns_MutexSetName(&lock, "dci:broadcast");
    
    /*
     * The retry interval is how often the broadcaster should attempt to
     * reconnect to dropped clients.
     */
     
    if (!Ns_ConfigGetInt(path, "retryinterval", &retryInterval) || retryInterval < 1) {
    	retryInterval = 2;  /* 2 seconds. */
    }

    /*
     * The warninterval is used to throttle the number of connection
     * errors dumped in the server log.  It should be substantially
     * higher than the retry interval.
     */
     
    if (!Ns_ConfigGetInt(path, "warninterval", &warnInterval) || warnInterval < 1) {
    	warnInterval = 120; /* 2 minutes. */
    }

    /*
     * The timeout interval puts a limit on how long we will try to send
     * a message to an unresponsive (yet connected) host. Once exceeded, the
     * message is dropped and the unresponsive host is put on the retry list.
     */
     
    if (!Ns_ConfigGetInt(path, "timeoutinterval", &timeoutInterval) || timeoutInterval < 1) {
    	timeoutInterval = -1; /* no timeout (blocks indefinitely) */
    }

    /*
     * If we have a unresponsive host, it can have the effect of bogging down
     * the whole system. After the first timeout is exceeded, this provides the
     * ability to allow a second, shorter timeout which will be used for that
     * client until a message is successfully sent. 
     */
     
    if (!Ns_ConfigGetInt(path, "secondarytimeout", &stimeoutInterval)) {
    	stimeoutInterval = timeoutInterval; /* default is same as primary timeout */
    }


    Ns_TclInitInterps(server, AddCmds, NULL);

    /* 
     * Register a procedure to initiate shutdown of all broadcasters. Note
     * this is not a ServerShutdown so it will be invoked until after the
     * server connection threads have exited.
     */
         
    Ns_RegisterShutdown(StopBroadcasters, NULL);
    return NS_OK;
}

static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "broadcast.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateObjCommand(interp, "broadcast.stats", StatsObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "broadcast.clients", ClientsObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "broadcast.dump", DumpObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "broadcast.names", NamesObjCmd, NULL, NULL);
    return TCL_OK;
}




/*
 *----------------------------------------------------------------------
 *
 * Dci_CreateBroadcaster --
 *
 *	Create a new broadcaster for the given clients.
 *
 * Results:
 *	Opaque pointer to Broadcaster structure.
 *
 * Side effects:
 *	Broadcaster thread will be created.
 *
 *----------------------------------------------------------------------
 */

Dci_Broadcaster *
Dci_CreateBroadcaster(char *name, void *clientData, Ns_Set *clients, Dci_MsgProc *initProc)
{
    Broadcast *bPtr;
    Client *cPtr, *firstPtr;
    char *host, *p, *client;
    int port, i;

    /*
     * The clients Ns_Set is a list of named clients with the value
     * in the form host:port.  The host should ideally be an IP
     * address to avoid DNS lookups.
     */
     
    firstPtr = NULL;
    for (i = 0; clients != NULL && i < Ns_SetSize(clients); ++i) {
    	client = Ns_SetKey(clients, i);
	host = Ns_SetValue(clients, i);
	p = strchr(host, ':');
	if (p == NULL || p == host || (port = atoi(p+1)) == 0) {
	    Ns_Log(Warning, "broadcast[%s]: invalid client: %s", name, client);
	}  else {
	    Ns_Log(Notice, "broadcast[%s]: adding client: %s", name, client);
	    *p = '\0';
	    cPtr = ns_calloc(1, sizeof(Client) + strlen(host));
	    cPtr->name = client;
    	    strcpy(cPtr->host, host);
	    cPtr->port = port;
	    cPtr->sock = -1;
	    cPtr->state = BCAST_CONNECT;
            cPtr->qtime = 0;
            cPtr->timeout = timeoutInterval;
	    cPtr->nextPtr = firstPtr;
	    firstPtr = cPtr;
	    *p = ':';
       }
    }
    
    /*
     * If any valid clients where found, create the broadcast object
     * and broadcaster thread.
     */
     
    if (firstPtr == NULL) {
    	Ns_Log(Error, "broadcast[%s]: no clients defined", name);
	bPtr = NULL;
    } else {
	bPtr = ns_calloc(1, sizeof(Broadcast) + strlen(name));
	bPtr->nextPtr = firstBroadcastPtr;
	firstBroadcastPtr = bPtr;
        Ns_MutexInit(&bPtr->lock);
        Ns_MutexSetName2(&bPtr->lock, "dci:broadcast", name);
    	strcpy(bPtr->name, name);
	bPtr->clientData = clientData;
	bPtr->initProc = initProc;
	bPtr->firstClientPtr = firstPtr;
	if (ns_pipe(bPtr->triggerPipe) != 0) {
	    Ns_Fatal("ns_pipe() failed: %s", strerror(errno));
	}
	Ns_ThreadCreate(DciBroadcaster, bPtr, 0, &bPtr->thread);
	Ns_Log(Notice, "broadcast[%s]: starting", name);
    }
    
    return (Dci_Broadcaster *) bPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_MsgData --
 *
 *	Return the data portion of a message.
 *
 * Results:
 *	Pointer to message data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Dci_MsgData(Dci_Msg *msg)
{
    Msg *msgPtr = (Msg *) msg;

    return (&msgPtr->hdr + 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_MsgAlloc --
 *
 *	Allocate a message structure with enough space for the given
 *  	size.
 *
 * Results:
 *	Pointer to message object.
 *
 * Side effects:
 *	Ref count is set to 1.
 *
 *----------------------------------------------------------------------
 */

Dci_Msg *
Dci_MsgAlloc(size_t size)
{
    Msg *msgPtr;

    msgPtr = ns_malloc(sizeof(Msg) + size);
    msgPtr->size = size;
    msgPtr->nextPtr = NULL;
    msgPtr->refcnt = 1;
    msgPtr->hdr = htonl(size);
    return (Dci_Msg *) msgPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_MsgDecr --
 *
 *	Decrement message ref count and free if no longer in use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Dci_MsgDecr(Dci_Msg *msg)
{
    Msg *msgPtr = (Msg *) msg;

    if (--msgPtr->refcnt == 0) {
	ns_free(msgPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_Broadcast --
 *
 *	Queue a message for send.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Message will be sent soon.
 *
 *----------------------------------------------------------------------
 */

void
Dci_Broadcast(Dci_Broadcaster *broadcast, Dci_Msg *msg)
{
    Broadcast *bPtr = (Broadcast *) broadcast;
    Msg *msgPtr = (Msg *) msg;
 
    msgPtr->nextPtr = NULL;    
    Ns_MutexLock(&lock);
    if (shutdownPending) {
	Ns_Log(Warning, "%s: shutdown pending - message dropped", bPtr->name);
	Dci_MsgDecr(msg);
    } else {
    	if (bPtr->firstMsgPtr == NULL) {
	
	    /*
	     * If this is the first message on an empty queue,
	     * trigger the broadcaster thread to wakeup.
	     */
	     
	    TriggerBroadcaster(bPtr);
    	    bPtr->firstMsgPtr = msgPtr;
    	} else {
    	    bPtr->lastMsgPtr->nextPtr = msgPtr;
        }
        bPtr->lastMsgPtr = msgPtr;
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * DciBroadcaster --
 *
 *	Thread to service message queue for a broadcaster.  The 
 *  	connection management code below is very similar to that
 *  	in the Dci_Server interface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Message on queue will be sent in order to connected clients.
 *
 *----------------------------------------------------------------------
 */

static void
DciBroadcaster(void *arg)
{
    Broadcast *bPtr;
    Client *cPtr, *nextPtr;
    Msg *msgPtr;
    Dci_Msg *login;
    int ok, busy, n, timeout;
    size_t nfds;
    char c;
    time_t retry, now;
    char buf[200];
    struct pollfd *pfds;

    /*
     * Wait for server startup to complete and then loop forever
     * until a shutdown message is received.
     */
     
    bPtr = arg;
    sprintf(buf, "broadcast:%s", bPtr->name);
    Ns_ThreadSetName(buf);
    Ns_WaitForStartup();
    login = Dci_MsgAlloc(strlen(bPtr->name)+1);
    strcpy(Dci_MsgData(login), bPtr->name);
    retry = 0;
    busy = 0;
    bPtr->firstRetryPtr = bPtr->firstClientPtr;
    bPtr->firstActivePtr = NULL;
    nfds = 1;
    cPtr = bPtr->firstRetryPtr;
    while (cPtr != NULL) {
	++nfds;	
	cPtr = cPtr->nextPtr;
    }
    pfds = ns_malloc(sizeof(struct pollfd) * nfds);
    pfds[0].fd = bPtr->triggerPipe[0];
    pfds[0].events = POLLIN;

    while (1) {

	/*
	 * If it's time to retry connections, push any clients
	 * waiting for reconnect back on the active list.
	 */

        Ns_MutexLock(&bPtr->lock);

	time(&now);
	if (bPtr->firstRetryPtr != NULL && retry <= now) {
	    retry = now + retryInterval;
	    cPtr = bPtr->firstRetryPtr;
	    bPtr->firstRetryPtr = NULL;
	    while (cPtr != NULL) {
		nextPtr = cPtr->nextPtr;
		cPtr->sock = Ns_SockAsyncConnect(cPtr->host, cPtr->port);
		if (cPtr->sock == -1) {
		    cPtr->nextPtr = bPtr->firstRetryPtr;
		    bPtr->firstRetryPtr = cPtr;
		} else {
		    cPtr->state = BCAST_CONNECT;
		    cPtr->nextPtr = bPtr->firstActivePtr;
		    bPtr->firstActivePtr = cPtr;
		}
		cPtr = nextPtr;
	    }
	}

	/*
	 * Set the poll events for all active clients and call poll().
	 */

	nfds = 1;
    	pfds[0].revents = 0;
	cPtr = bPtr->firstActivePtr;
	while (cPtr != NULL) {
	    cPtr->pfd = &pfds[nfds++];
	    cPtr->pfd->fd = cPtr->sock;
	    cPtr->pfd->events = cPtr->pfd->revents = 0;
	    if (cPtr->state == BCAST_CONNECT || cPtr->msgPtr != NULL) {
	    	cPtr->pfd->events |= POLLOUT;
	    }
	    if (cPtr->state != BCAST_CONNECT) {
	    	cPtr->pfd->events |= POLLIN;
	    }
	    cPtr = cPtr->nextPtr;
	}
	do {
	    if (bPtr->firstRetryPtr == NULL && msgPtr == NULL) {
		timeout = -1;
	    } else {
		timeout = (retry - time(NULL)) * 1000;
	    	if (timeout < 0) {
		    timeout = 0;
		}
	    }
	    n = poll(pfds, nfds, timeout);
	} while (n < 0 && errno == EINTR);
	if (n < 0) {
	    Ns_Fatal("poll() failed: %s", strerror(errno));
	}

	/*
	 * Drain the trigger pipe.
	 */

	if ((pfds[0].revents & POLLIN) && read(bPtr->triggerPipe[0], &c, 1) != 1) {
	    Ns_Fatal("trigger pipe read() failed: %s", strerror(errno));
 	}

	/*
	 * Verify all sockets and attempt to send any remaining bytes.
	 */

    	busy = 0;
	cPtr = bPtr->firstActivePtr;
	bPtr->firstActivePtr = NULL;
	while (cPtr != NULL) {
	    nextPtr = cPtr->nextPtr;
	    ok = 1;
	    if (cPtr->state == BCAST_CONNECT) {
	    	if (cPtr->pfd->revents & POLLOUT || cPtr->pfd->revents & POLLHUP) {
		    if (send(cPtr->sock, "", 0, 0) == 0) {
    	    		Ns_Log(Notice, "%s: connected to %s:%d", 
                            cPtr->name, cPtr->host, cPtr->port);
			Dci_SockOpts(cPtr->sock, DCI_SOCK_SERVER);
			MsgSend(cPtr, (Msg *) login);
			cPtr->state = BCAST_LOGIN;
		    } else {
			if (cPtr->nextwarn <= now) {
		    	    Ns_Log(Warning, "%s: connect to %s:%d failed",
				cPtr->name, cPtr->host, cPtr->port);
			    cPtr->nextwarn = now + warnInterval;
			}
			ok = 0;
		    }
		}
	    } else if (cPtr->pfd->revents & POLLIN) {
    	    	Ns_Log(Notice, "%s: dropped", cPtr->name);
    	    	ok = 0;
	    } else if (cPtr->msgPtr != NULL) {
                if (cPtr->pfd->revents & POLLOUT) {
                    n = send(cPtr->sock, cPtr->bufPtr, (size_t)cPtr->nsend, 0);
                    if (n < 0) {
                        Ns_Log(Error, "%s: send() failed: %s", cPtr->name,
                               strerror(errno));
                        ok = 0;
                    } else if (n == 0) {
                        Ns_Fatal("%s: send() returned 0", cPtr->name);
                    } else {
                        cPtr->bufPtr += n;
                        cPtr->nsend -= n;
                        if (cPtr->nsend == 0) {
                            if (fDebug) {
                                Ns_Log(Notice,"%s: send complete", cPtr->name);
                            }
                            cPtr->timeout = timeoutInterval;
                            MsgDone(cPtr);
                            if (cPtr->state == BCAST_LOGIN && bPtr->initProc != NULL) {
                                msgPtr = (Msg *) (*bPtr->initProc)(bPtr->clientData);
                                if (msgPtr != NULL) {
                                    MsgSend(cPtr, msgPtr);
                                }
                                cPtr->state = BCAST_READY;
                            }
                        }
                    }
		} else {
                    if (cPtr->timeout >= 0) {
                        /*
                         * If a host is unresponsive, we need to consider it
                         * down and keep it from blocking the rest of the messages
                         * to the rest of the servers 
                         */
                        if (now >= (cPtr->qtime + cPtr->timeout)) {
                            if (cPtr->nextwarn <= now || fDebug) {
                                Ns_Log(Error,"%s: Send timeout to %s:%d. Message dropped",cPtr->name, cPtr->host, cPtr->port);
                                cPtr->nextwarn = now + warnInterval;
                            }
                            MsgDone(cPtr);
                            /* For this error, we don't want to close out the socket and
                             * open a new one (potentially ad infinitum since this case
                             * often occurs when the socket buffer fills)
                             * However, we allow the option of shortening subsequent
                             * timeouts until there a successful send, and we'll 
                             * treat as reconnect.
                             */
                            cPtr->timeout = stimeoutInterval;
                            cPtr->state = BCAST_CONNECT;
                        }

                    }
                }
	    } 
	    if (ok) {
		cPtr->nextPtr = bPtr->firstActivePtr;
		bPtr->firstActivePtr = cPtr;
		if (cPtr->msgPtr != NULL) {
		    busy = 1;
		}
	    } else {
		RetryClient(cPtr, &bPtr->firstRetryPtr);
	    }
	    cPtr = nextPtr;
	}

        Ns_MutexUnlock(&bPtr->lock);

	/*
	 * If the broadcaster isn't busy (i.e., last message was
	 * sent to all clients), setup the clients to receive the
	 * next message.
	 */

    	if (!busy) {
	    Ns_MutexLock(&lock);
	    if (shutdownPending) {
		break;
	    }
	    msgPtr = bPtr->firstMsgPtr;
	    if (msgPtr != NULL) {
	    	bPtr->firstMsgPtr = bPtr->firstMsgPtr->nextPtr;
		if (bPtr->lastMsgPtr == msgPtr) {
		    bPtr->lastMsgPtr = NULL;
		}
	    }
	    Ns_MutexUnlock(&lock);
	    if (msgPtr != NULL) {
		cPtr = bPtr->firstActivePtr;
		while (cPtr != NULL) {
	    	    if (cPtr->state != BCAST_CONNECT) {
		    	MsgSend(cPtr, msgPtr);
		    }
		    cPtr = cPtr->nextPtr;
		}
		
		/*
		 * Decrement the ref count once here so it will be
		 * freed when last client is done with the message.
		 */
		 
		Dci_MsgDecr((Dci_Msg *) msgPtr);
	    }
	}
    }

    /*
     * Shutdown is pending so close all clients and dump any
     * pending messages.
     */

    if (bPtr->firstMsgPtr != NULL) {
	n = 0;
        while ((msgPtr = bPtr->firstMsgPtr) != NULL) {
	    bPtr->firstMsgPtr = msgPtr->nextPtr;
	    Dci_MsgDecr((Dci_Msg *) msgPtr);
	    ++n;
        }
	Ns_Log(Warning, "stopping - %d pending message%s dropped",
	    n, n == 1 ? "" : "s");
    }
    Ns_MutexUnlock(&lock);
    while ((cPtr = bPtr->firstActivePtr) != NULL) {
	bPtr->firstActivePtr = cPtr->nextPtr;
	RetryClient(cPtr, &bPtr->firstRetryPtr);
    }
    while ((cPtr = bPtr->firstRetryPtr) != NULL) {
	bPtr->firstRetryPtr = cPtr->nextPtr;
	ns_free(cPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StopBroadcaster --
 *
 *	Shutdown callback to trigger and wait for all broadcasters to
 *  	stop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
StopBroadcasters(void *ignored)
{
    Broadcast *bPtr;

    Ns_MutexLock(&lock);
    shutdownPending = 1;
    Ns_MutexUnlock(&lock);

    bPtr = firstBroadcastPtr;
    while (bPtr != NULL) {
	Ns_Log(Notice, "%s: triggering shutdown", bPtr->name);
	TriggerBroadcaster(bPtr);
	bPtr = bPtr->nextPtr;
    }
    while ((bPtr = firstBroadcastPtr) != NULL) {
	firstBroadcastPtr = bPtr->nextPtr;
	Ns_ThreadJoin(&bPtr->thread, NULL);
	Ns_Log(Notice, "%s: shutdown complete", bPtr->name);
        Ns_MutexDestroy(&bPtr->lock);
	close(bPtr->triggerPipe[0]);
	close(bPtr->triggerPipe[1]);
	ns_free(bPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * MsgDone, MsgSend --
 *
 *	Utilities to setup clients to begin or end a message send.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
MsgDone(Client *cPtr)
{
    Dci_MsgDecr((Dci_Msg *) cPtr->msgPtr);
    cPtr->msgPtr = NULL;
}


static void
MsgSend(Client *cPtr, Msg *msgPtr)
{
    ++msgPtr->refcnt;
    cPtr->msgPtr = msgPtr;
    cPtr->bufPtr = (char *) &msgPtr->hdr; 
    cPtr->nsend = msgPtr->size + sizeof(uint32_t);
    time(&cPtr->qtime);
}


/*
 *----------------------------------------------------------------------
 *
 * TriggerBroadcaster --
 *
 *	Write a byte down the trigger pipe to wakeup a broadcast
 *  	thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Broadcast thread will wakeup and check queue.
 *
 *----------------------------------------------------------------------
 */

static void
TriggerBroadcaster(Broadcast *bPtr)
{
    if (write(bPtr->triggerPipe[1], "", 1) != 1) {
	Ns_Fatal("%s: trigger write() failed: %s", bPtr->name, strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RetryClient --
 *
 *	Setup a client to retry it's connection on failure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RetryClient(Client *cPtr, Client **firstRetryPtrPtr)
{
    close(cPtr->sock);
    cPtr->sock = -1;
    cPtr->nextPtr = *firstRetryPtrPtr;
    *firstRetryPtrPtr = cPtr;
    if (cPtr->msgPtr != NULL) {
	MsgDone(cPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StatsObjCmd --
 *
 *	Return statistics about a broadcaster's state.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
StatsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Broadcast *bPtr;
    Client *cPtr;
    Msg *msgPtr;
    int n;

    Ns_MutexLock(&lock);
    if (shutdownPending) {
        Tcl_AppendResult(interp, "shutdown pending", NULL);
        Ns_MutexUnlock(&lock);
        return TCL_ERROR;
    }

    bPtr = firstBroadcastPtr;
    while (bPtr != NULL) {
        Tcl_Obj *objPtr = Tcl_NewObj();
        Tcl_Obj *cObjPtr = Tcl_NewObj();

        Tcl_ListObjAppendElement(interp, objPtr,
                Tcl_NewStringObj(bPtr->name, -1));

        n = 0;
        msgPtr = bPtr->firstMsgPtr;
        while (msgPtr != NULL) {
            n++;
            msgPtr = msgPtr->nextPtr;
        }
        Tcl_ListObjAppendElement(interp, objPtr, Tcl_NewIntObj(n));

	TriggerBroadcaster(bPtr);
        Ns_MutexLock(&bPtr->lock);
        cPtr = bPtr->firstActivePtr;
        while (cPtr != NULL) {
            if (cPtr->msgPtr != NULL) {
                Tcl_ListObjAppendElement(interp, cObjPtr,
                        Tcl_NewStringObj(cPtr->name, -1));
                Tcl_ListObjAppendElement(interp, cObjPtr,
                        Tcl_NewIntObj(cPtr->qtime));
                break;
            }
            cPtr = cPtr->nextPtr;
        }
        Ns_MutexUnlock(&bPtr->lock);
        Tcl_ListObjAppendElement(interp, objPtr, cObjPtr);

        Tcl_ListObjAppendElement(interp, Tcl_GetObjResult(interp), objPtr);
        bPtr = bPtr->nextPtr;
    }

    Ns_MutexUnlock(&lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ClientsObjCmd --
 *
 *	Return info about clients for a broadcaster.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ClientInfoListObj(Tcl_Interp *interp, Tcl_Obj *listPtr, Client *cPtr)
{
    char *state;

    switch (cPtr->state) {
    case BCAST_READY: state = "ready"; break;
    case BCAST_CONNECT: state = "connect"; break;
    case BCAST_LOGIN: state = "login"; break;
    default: state = "unknown"; break;
    }

    Tcl_ListObjAppendElement(interp, listPtr,
            Tcl_NewStringObj(cPtr->name, -1));
    Tcl_ListObjAppendElement(interp, listPtr,
            Tcl_NewStringObj(cPtr->host, -1));
    Tcl_ListObjAppendElement(interp, listPtr,
            Tcl_NewStringObj(state, -1));

    return TCL_OK;
}

static int
NamesObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Broadcast *bPtr;

    bPtr = firstBroadcastPtr;
    while (bPtr != NULL) {
        Tcl_Obj *objPtr = Tcl_NewObj();

        Tcl_ListObjAppendElement(interp, objPtr, Tcl_NewStringObj(bPtr->name, -1)); 
        Tcl_ListObjAppendElement(interp, Tcl_GetObjResult(interp), objPtr);

        bPtr = bPtr->nextPtr;
    }

    return TCL_OK;
}

static int
ClientsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Broadcast *bPtr;
    Client *cPtr;
    char *name;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "broadcaster");
        return TCL_ERROR;
    }

    Ns_MutexLock(&lock);
    if (shutdownPending) {
        Tcl_AppendResult(interp, "shutdown pending", NULL);
        Ns_MutexUnlock(&lock);
        return TCL_ERROR;
    }

    name = Tcl_GetString(objv[1]);

    bPtr = firstBroadcastPtr;
    while (bPtr != NULL) {
        if (STREQ(bPtr->name, name)) {
            break;
        }
        bPtr = bPtr->nextPtr;
    }

    if (bPtr == NULL) {
        Tcl_AppendResult(interp, "no such broadcaster: ", name, NULL);
        Ns_MutexUnlock(&lock);
        return TCL_ERROR;
    }
        
    TriggerBroadcaster(bPtr);
    Ns_MutexLock(&bPtr->lock);
    cPtr = bPtr->firstRetryPtr;
    while (cPtr != NULL) {
        Tcl_Obj *objPtr = Tcl_NewObj();

        ClientInfoListObj(interp, objPtr, cPtr);
        Tcl_ListObjAppendElement(interp, Tcl_GetObjResult(interp),
                objPtr);

        cPtr = cPtr->nextPtr;
    }

    cPtr = bPtr->firstActivePtr;
    while (cPtr != NULL) {
        Tcl_Obj *objPtr = Tcl_NewObj();

        ClientInfoListObj(interp, objPtr, cPtr);
        Tcl_ListObjAppendElement(interp, Tcl_GetObjResult(interp),
                objPtr);

        cPtr = cPtr->nextPtr;
    }
    Ns_MutexUnlock(&bPtr->lock);

    Ns_MutexUnlock(&lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DumpObjCmd --
 *
 *	Return the contents of a broadcaster's queue.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
DumpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Broadcast *bPtr;
    Msg *msgPtr;
    char *name;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "broadcaster");
        return TCL_ERROR;
    }

    Ns_MutexLock(&lock);
    if (shutdownPending) {
        Tcl_AppendResult(interp, "shutdown pending", NULL);
        Ns_MutexUnlock(&lock);
        return TCL_ERROR;
    }

    name = Tcl_GetString(objv[1]);

    bPtr = firstBroadcastPtr;
    while (bPtr != NULL) {
        if (STREQ(bPtr->name, name)) {
            break;
        }
        bPtr = bPtr->nextPtr;
    }

    if (bPtr == NULL) {
        Tcl_AppendResult(interp, "no such broadcaster: ", name, NULL);
        Ns_MutexUnlock(&lock);
        return TCL_ERROR;
    }

    msgPtr = bPtr->firstMsgPtr;
    while (msgPtr != NULL) {
        Tcl_Obj *objPtr = Tcl_NewObj();

        Tcl_ListObjAppendElement(interp, objPtr,
                Tcl_NewIntObj(msgPtr->size));
        Tcl_ListObjAppendElement(interp, objPtr,
                Tcl_NewIntObj((int) msgPtr->hdr));
        Tcl_ListObjAppendElement(interp, objPtr,
                Tcl_NewStringObj((char *) &msgPtr->hdr
                    + sizeof(msgPtr->hdr), msgPtr->size));

        Tcl_ListObjAppendElement(interp, Tcl_GetObjResult(interp),
                objPtr);

        msgPtr = msgPtr->nextPtr;
    }
        
    Ns_MutexUnlock(&lock);
    return TCL_OK;
}
