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
#include <sys/ioctl.h>

/*
 * The following structure defines a client the server
 * should connect to and process sob fetch requests.
 */

typedef struct Stats {
    unsigned long nrecv;
    unsigned long nsend;
    unsigned long nicb;
    unsigned long ndcb;
    unsigned long nrcb;
    unsigned long nscb;
} Stats;

typedef struct Client {
    struct Client *nextClientPtr;
    struct Client *nextPtr;
    char  *name;
    char  *host;
    int    port;
    void  *clientData;

    int sock;
    struct pollfd *pfd;
    time_t nextwarn;
    enum {
    	Connecting, RecvData, SendData
    } state;

    /*
     * statistics.
     */

    time_t ctime;
    time_t mtime;
    Stats stats;
    
    /*
     * send/recv I/O remaining.
     */

    int nleft;

    /*
     * sendmsg() buffering header.
     */
     
    struct msghdr msg;

    /*
     * iovec for single buffer sends.
     */

    struct iovec iov[1];
    
    /*
     * recv() read-ahead buffers and output copy pointer.
     */

    char *bufPtr;
    char *recvbase;
    int   recvcnt;
    char  recvbuf[1];

} Client;

/*
 * The following structure defines a server thread.
 */

typedef struct Server {
    void   *serverData;
    void   *cmdData;
    int triggerPipe[2];
    Dci_ServerProc *procPtr;
    Client *firstClientPtr;
    Ns_Thread thread;
    Stats stats;
    char name[1];
} Server;

static Tcl_CmdProc NamesCmd;
static Tcl_CmdProc StatsCmd;
static Ns_TclInterpInitProc AddCmds;
static Tcl_HashTable serverTable;
static Ns_ThreadProc DciServer;
static Ns_Callback StopServers;
static int fDebug;
static int warnInterval;
static int retryInterval;
static int bufsize;


/*
 *----------------------------------------------------------------------
 *
 * DciServerInit --
 *
 *      This function initializes several static global variables:
 *
 *      warnInterval - the seconds between logged warnings.
 *      retryInterval - the number of seconds between connection 
 *          attempts after drops.
 *      bufsize - the default size in bytes of the read buffer, used
 *          in an attempt to gather all incoming data (of unknown size)
 *          in a single recv call.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
DciServerInit(char *server, char *module)
{
    char *path;

    Dci_LogIdent(module, rcsid);
    Tcl_InitHashTable(&serverTable, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "server", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
	fDebug = 1;
    }
    if (!Ns_ConfigGetInt(path, "warninterval", &warnInterval)) {
	warnInterval = 60;
    }
    if (!Ns_ConfigGetInt(path, "retryinterval", &retryInterval)) {
    	retryInterval = 5;
    }
    if (!Ns_ConfigGetInt(path, "bufsize", &bufsize)) {
    	bufsize = 16000;
    }
    Ns_RegisterAtShutdown(StopServers, NULL);
    Ns_TclInitInterps(server, AddCmds, NULL);
    return NS_OK;
}

static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "server.names", NamesCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "server.stats", StatsCmd, NULL, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_CreateServer --
 *
 *      Dci_CreateServer() creates a network based server, typically
 *      called from Dci_RpcCreateServer() - see rpc.c in this 
 *      directory. Servers run in their own non-detached thread and
 *      are joined in StopServers() in order to fulfill existing
 *      requests at server shutdown. The thread function is 
 *      DciServer() explained below.
 *
 *      The client set is an Ns_Set of the clients to connect to, and
 *      is in the form:
 *          key:<name> value:<host:port>
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      On success, a non-detached thread is created as well as 
 *      trigger pipes used for inter-thread communication to the
 *      server thread.
 *
 *----------------------------------------------------------------------
 */

int
Dci_CreateServer(char *server, void *serverData, Ns_Set *clients, Dci_ServerProc *procPtr)
{
    Client *cPtr, *firstPtr;
    Server *sPtr;
    char *host, *p, *client;
    int port, i, new;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_CreateHashEntry(&serverTable, server, &new);
    if (!new) {
	Ns_Log(Warning, "server[%s]: already exists", server);
    	return NS_ERROR;
    }
    
    firstPtr = NULL;
    for (i = 0; clients != NULL && i < Ns_SetSize(clients); ++i) {
    	client = Ns_SetKey(clients, i);
	host = Ns_SetValue(clients, i);
	p = strchr(host, ':');
	if (p == NULL || p == host || (port = atoi(p+1)) == 0) {
	    Ns_Log(Notice, "server[%s]: invalid client: %s",
    	    	server, client);
	}  else {
	    Ns_Log(Notice, "server[%s]: creating client: %s",
	    	server, client);
	    *p = '\0';
	    cPtr = ns_calloc(1, sizeof(Client) + bufsize);
	    cPtr->host = ns_strdup(host);
	    cPtr->name = client;
	    cPtr->port = port;
	    cPtr->sock = -1;
	    cPtr->state = Connecting;
	    cPtr->nextwarn = 0;
	    cPtr->nextClientPtr = cPtr->nextPtr = firstPtr;
	    firstPtr = cPtr;
	    *p = ':';
	}
    }
    if (firstPtr == NULL) {
	Ns_Log(Warning, "server[%s]: no valid clients", server);
	Tcl_DeleteHashEntry(hPtr);
	return NS_ERROR;
    }
    
    sPtr = ns_calloc(1, sizeof(Server) + strlen(server));
    strcpy(sPtr->name, server);
    if (ns_pipe(sPtr->triggerPipe) != 0) {
    	Ns_Fatal("ns_pipe() failed: %s", strerror(errno));
    }
    sPtr->serverData = serverData;
    sPtr->procPtr = procPtr;
    sPtr->firstClientPtr = firstPtr;
    Tcl_SetHashValue(hPtr, sPtr);
    Ns_ThreadCreate(DciServer, sPtr, 0, &sPtr->thread);
    Ns_Log(Notice, "server[%s]: initialized", server);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerName --
 *
 *      Returns the name of the client.
 *
 * Results:
 *      Pointer to client name.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Dci_ServerGetName(Dci_Client *client)
{
    Client *cPtr = (Client *) client;
    
    return cPtr->name;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerGetData --
 *
 *      Returns the clientData member of a Dci_Client struct for a
 *      given server. This command is typically called from the
 *      Dci_ServerProc passed to Dci_CreateServer.
 *
 * Results:
 *      Pointer to clientData.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void *
Dci_ServerGetData(Dci_Client *client)
{
    Client *cPtr = (Client *) client;
    
    return cPtr->clientData;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerSetData --
 *
 *      Sets the clientData member of a Dci_Client struct for a
 *      given server. This command is typically called from the
 *      Dci_ServerProc passed to Dci_CreateServer..
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the client's clientData member.
 *
 *----------------------------------------------------------------------
 */

void
Dci_ServerSetData(Dci_Client *client, void *dataPtr)
{
    Client *cPtr = (Client *) client;

    cPtr->clientData = dataPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerSendVec --
 *
 *      Dci_ServerSendVec queues the given iovec struct for send within
 *      DciServer() - see below.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates the state of a client connection to "SendData", as well
 *      as the Dci_Client msg member with the passed iovec struct.
 *
 *----------------------------------------------------------------------
 */

void
Dci_ServerSendVec(Dci_Client *client, struct iovec *iov, int iovcnt)
{
    Client *cPtr = (Client *) client;
    int i;
    
    cPtr->bufPtr = NULL;
    cPtr->nleft = 0;
    for (i = 0; i < iovcnt; ++i) {
    	cPtr->nleft += iov[i].iov_len;
    }
    cPtr->msg.msg_iov = iov;
    cPtr->msg.msg_iovlen = iovcnt;
    cPtr->state = SendData;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerSend --
 *
 *      Dci_ServerSend modifies the Dci_Client struct to store the 
 *      passed buffer, then calls Dci_ServerSendVec() to queue the msg
 *      to be sent to the client.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates the Dci_Client iov member with the passed bufPtr.
 *
 *----------------------------------------------------------------------
 */

void
Dci_ServerSend(Dci_Client *client, void *bufPtr, int len)
{
    Client *cPtr = (Client *) client;
    
    cPtr->iov[0].iov_base = bufPtr;
    cPtr->iov[0].iov_len = len;
    Dci_ServerSendVec(client, cPtr->iov, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ServerRecv --
 *
 *      Dci_ServerRecv updates the Dci_Client struct to store bufPtr
 *      as the recieve buffer, and places the state to "RecvData", 
 *      queueing the server for recieve mode in DciServer() below.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the Dci_Client recieve buffer to point to bufPtr, as
 *      well as queue the client for recieve mode.
 *
 *----------------------------------------------------------------------
 */

void
Dci_ServerRecv(Dci_Client *client, void *bufPtr, int len)
{
    Client *cPtr = (Client *) client;

    cPtr->bufPtr = (char *) bufPtr;
    cPtr->nleft = len;
    cPtr->state = RecvData;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnectWarn --
 *
 *      Logging facility to log failed connection attempts with a
 *      frequency determined by "warnInterval" set in DciServerInit().
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If the "warnInterval" has passed, a warning will be logged.
 *
 *----------------------------------------------------------------------
 */

static void
ConnectWarn(Client *cPtr, time_t now)
{
    if (cPtr->nextwarn <= now) {
	Ns_Log(Warning, "%s: connect to %s:%d failed", 
            cPtr->name, cPtr->host, cPtr->port);
	cPtr->nextwarn = now + warnInterval;
    }
}
    

/*
 *----------------------------------------------------------------------
 *
 * DropClient --
 *
 *      DropClient closes the connection to a given client.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The server's Dci_ServerProc is called with DCI_SERVER_DROP as
 *      the why argument for further processing.
 *
 *----------------------------------------------------------------------
 */

static void
DropClient(Server *sPtr, Client *cPtr)
{
    close(cPtr->sock);
    cPtr->sock = -1;
    ++sPtr->stats.ndcb;
    ++cPtr->stats.ndcb;
    (void) (*sPtr->procPtr)((Dci_Client *) cPtr, sPtr->serverData, DCI_SERVER_DROP);
    Ns_Log(Warning, "%s: dropped", cPtr->name);
}


/*
 *----------------------------------------------------------------------
 *
 * Callback --
 *
 *      This is the callback used to handle read and write requests
 *      for a server.
 *
 *      NB: The buffer size used to recieve incoming data is a guess.
 *      It defaults to 16k bytes in an attempt to read the entire
 *      message in one recv() thereby reducing the number of system
 *      calls.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Data is sent/recieved from/to supplied buffers within the
 *      Client struct.
 *
 *----------------------------------------------------------------------
 */

static int
Callback(Server *sPtr, Client *cPtr, int why)
{
    int i, n;

    if (why == DCI_SERVER_SEND) {
	n = sendmsg(cPtr->sock, &cPtr->msg, 0);
    } else {
	if (bufsize == 0) {
	    n = recv(cPtr->sock, cPtr->bufPtr, (size_t)cPtr->nleft, 0);
	} else {
	    if (cPtr->recvcnt == 0) {
		cPtr->recvbase = cPtr->recvbuf;
		cPtr->recvcnt = recv(cPtr->sock, cPtr->recvbuf, (size_t)bufsize, 0);
	    }
	    if (cPtr->recvcnt <= 0) {
		n = cPtr->recvcnt;
		cPtr->recvcnt = 0;
	    } else {
		if (cPtr->recvcnt > cPtr->nleft) {
		    n = cPtr->nleft;
		} else {
		    n = cPtr->recvcnt;
		}
		memcpy(cPtr->bufPtr, cPtr->recvbase, (size_t)n);
		cPtr->recvbase += n;
		cPtr->recvcnt -= n;
	    }
	}
    }
    if (n == 0) {
        return NS_ERROR;
    } else if (n < 0) {
        if (errno == EWOULDBLOCK) {
            return NS_TIMEOUT;
        } else {
            return NS_ERROR;
        }
    }
    cPtr->nleft -= n;
    if (why == DCI_SERVER_RECV) {
	cPtr->bufPtr += n;
	cPtr->stats.nrecv += n;
	sPtr->stats.nrecv += n;
    } else {
	cPtr->stats.nsend += n;
	sPtr->stats.nsend += n;
	for (i = 0; i < cPtr->msg.msg_iovlen && n > 0; ++i) {
    	    if (cPtr->msg.msg_iov[i].iov_len > 0) {
		if (n >= cPtr->msg.msg_iov[i].iov_len) {
		    n -= cPtr->msg.msg_iov[i].iov_len;
		    cPtr->msg.msg_iov[i].iov_len = 0;
		    cPtr->msg.msg_iov[i].iov_base = NULL;
		} else {
    		    cPtr->msg.msg_iov[i].iov_len -= n;
		    cPtr->msg.msg_iov[i].iov_base = (char *) cPtr->msg.msg_iov[i].iov_base + n;
		}
	    }
	}
    }
    if (cPtr->nleft == 0) {
	switch (why) {
	case DCI_SERVER_RECV:
	    ++cPtr->stats.nrcb;
	    ++sPtr->stats.nrcb;
	    break;
	case DCI_SERVER_SEND:
	    ++cPtr->stats.nscb;
	    ++sPtr->stats.nscb;
	    break;
	}
	return ((*sPtr->procPtr)((Dci_Client *) cPtr, sPtr->serverData, why));
    }
    
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DciServer --
 *
 *      This is the core server function called from Ns_ThreadCreate()
 *      via Dci_CreateServer(). Its function is to encapsulate the
 *      grungy connect/send/recieve loop of a normal server process.
 *      In essence this is "Dci_ServerMain".
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes all network based aspects of a server.
 *
 *----------------------------------------------------------------------
 */

static void
DciServer(void *arg)
{
    Server *sPtr = (Server *) arg;
    Client *cPtr, *nextPtr;
    Client *firstActivePtr, *firstRetryPtr;
    struct pollfd *pfds;
    int res, ok, nfds, timeout;
    char c;
    time_t retry, now;

    Ns_ThreadSetName(sPtr->name);

    /*
     * Wait for startup so servers can complete initialization
     * of their serverData if necessary.
     */
    Ns_WaitForStartup();

    retry = 0;
    firstRetryPtr = sPtr->firstClientPtr;
    nfds = 1;
    for (cPtr = firstRetryPtr; cPtr != NULL; cPtr = cPtr->nextPtr) {
	++nfds;
    }
    pfds = ns_malloc(sizeof(struct pollfd) * nfds);
    pfds[0].fd = sPtr->triggerPipe[0];
    pfds[0].events = POLLIN;
    firstActivePtr = NULL;
    while (1) {

	/*
	 * If it's time to retry connections, push any clients
	 * waiting for reconnect back on the active list.
	 */

	time(&now);
	if (firstRetryPtr != NULL && retry <= now) {
	    retry = now + retryInterval;
	    cPtr = firstRetryPtr;
	    firstRetryPtr = NULL;
	    while (cPtr != NULL) {
		nextPtr = cPtr->nextPtr;
		cPtr->sock = Ns_SockAsyncConnect(cPtr->host, cPtr->port);
		if (cPtr->sock == -1) {
		    ConnectWarn(cPtr, now);
		    cPtr->nextPtr = firstRetryPtr;
		    firstRetryPtr = cPtr;
		} else {
		    cPtr->state = Connecting;
		    cPtr->nextPtr = firstActivePtr;
		    firstActivePtr = cPtr;
		}
		cPtr = nextPtr;
	    }
	}

	/*
	 * Set the events for all active clients and call poll().
	 */

	nfds = 1;
	cPtr = firstActivePtr;
	while (cPtr != NULL) {
	    cPtr->pfd = &pfds[nfds++];
	    cPtr->pfd->fd = cPtr->sock;
	    if (cPtr->state == RecvData) {
		cPtr->pfd->events = POLLIN;
	    } else {
		cPtr->pfd->events = POLLOUT;
	    }
	    cPtr->pfd->revents = 0;
	    cPtr = cPtr->nextPtr;
	}

	do {
	    if (firstRetryPtr == NULL) {
		timeout = -1;
	    } else {
	    	timeout = (retry - time(NULL)) * 1000;
	    	if (timeout < 0) {
		    timeout = 0;
	    	}
	    }
	    ok = poll(pfds, (size_t) nfds, timeout);
	} while (ok < 0 && errno == EINTR);
	if (ok < 0) {
	    Ns_Fatal("poll() failed: %s", strerror(errno));
	}
    	if (pfds[0].revents & POLLIN) {
	    if (read(sPtr->triggerPipe[0], &c, 1) != 1) {
	    	Ns_Fatal("trigger read() failed: %s", strerror(errno));
	    }
	    break;
	}

	/*
	 * Process all clients, pushing failed clients onto the
	 * retry list.
	 */

	time(&now);
	cPtr = firstActivePtr;
	firstActivePtr = NULL;
	while (cPtr != NULL) {
	    res = NS_OK;
	    if (cPtr->state == Connecting) {
	    	if (cPtr->pfd->revents & POLLOUT || cPtr->pfd->revents & POLLHUP) {
		    if (send(cPtr->sock, NULL, 0, 0) != 0) {
		    	res = NS_ERROR;
		    } else {
    	    	    	Ns_Log(Notice, "%s: connected to %s:%d", 
                            cPtr->name, cPtr->host, cPtr->port);
			Dci_SockOpts(cPtr->sock, DCI_SOCK_SERVER);
			Ns_SockSetNonBlocking(cPtr->sock);
    			cPtr->recvcnt = 0;
			cPtr->ctime = now;
			cPtr->mtime = 0;
			++sPtr->stats.nicb;
			++cPtr->stats.nicb;
			res = (*sPtr->procPtr)((Dci_Client *) cPtr,
				sPtr->serverData, DCI_SERVER_INIT);
		    }
		}
	    } else {
	    	if (cPtr->state == RecvData && (cPtr->pfd->revents & POLLIN)) {
    	    	    /*
		     * Continue calling recv callbacks as long as recv()
		     * does not block or fail.
		     */

		    cPtr->mtime = now;
		    do {
		    	res = Callback(sPtr, cPtr, DCI_SERVER_RECV);
		    } while (res == NS_OK && cPtr->state == RecvData);

    	    	    /*
		     * If the recv callback switched the socket into the
		     * send state, jump directly to send now to avoid a
		     * needless trip through poll().  This optimization
		     * takes advantage of the common use of this interface
		     * for quick writes after reads.
		     */

		    if (res == NS_OK && cPtr->state == SendData) {
		    	goto sendnow;
		    }
		}
		
		if (cPtr->state == SendData && (cPtr->pfd->revents & POLLOUT)) {
sendnow:
    	    	    /*
		     * Continue calling send callbacks as long as send()
		     * does not block or fail.  Note that unlike above
		     * an immediate recv is not attempted if the send callback
		     * sets the socket in recv mode.  The assumption is that
		     * an immediate recv would likely block as the client
		     * normally must process the message just sent before
		     * sending a new request.
		     */

		    cPtr->mtime = now;
		    do {
		    	res = Callback(sPtr, cPtr, DCI_SERVER_SEND);
		    } while (res == NS_OK && cPtr->state == SendData);
		}
	    }
	    nextPtr = cPtr->nextPtr;
	    if (res == NS_OK || res == NS_TIMEOUT) {
		cPtr->nextPtr = firstActivePtr;
		firstActivePtr = cPtr;
	    } else {
    	    	if (cPtr->state == Connecting) {
		    close(cPtr->sock);
		    ConnectWarn(cPtr, now);
		} else {
		    DropClient(sPtr, cPtr);
		}
		cPtr->sock = -1;
		cPtr->nextPtr = firstRetryPtr;
		firstRetryPtr = cPtr;
	    }
	    cPtr = nextPtr;
	}
    }
    
    while ((cPtr = firstActivePtr) != NULL) {
	firstActivePtr = cPtr->nextPtr;
	DropClient(sPtr, cPtr);
	cPtr->nextPtr = firstRetryPtr;
	firstRetryPtr = cPtr;
    }
    while ((cPtr = firstRetryPtr) != NULL) {
	firstRetryPtr = cPtr->nextPtr;
	ns_free(cPtr->host);
	ns_free(cPtr);
    }

    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * StopServers --
 *
 *      StopServers is the exit procedure used to clean up a server
 *      at shutdown.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The server thread is joined so that it can fulfill existing
 *      requests during shutdown. Memory associated with the server
 *      is free'd.
 *
 *----------------------------------------------------------------------
 */

static void
StopServers(void *ignored)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Server *sPtr;

    hPtr = Tcl_FirstHashEntry(&serverTable, &search);
    while (hPtr != NULL) {
	sPtr = Tcl_GetHashValue(hPtr);
	Ns_Log(Notice, "%s: shutdown pending", sPtr->name);
    	if (write(sPtr->triggerPipe[1], "", 1) != 1) {
    	    Ns_Fatal("trigger write() failed: %s", strerror(errno));
    	}
	Ns_ThreadJoin(&sPtr->thread, NULL);
	close(sPtr->triggerPipe[0]);
	close(sPtr->triggerPipe[1]);
	Ns_Log(Notice, "%s: shutdown complete", sPtr->name);
	ns_free(sPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


static int
NamesCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *key, *pattern;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    pattern = argv[1];
    hPtr = Tcl_FirstHashEntry(&serverTable, &search);
    while (hPtr != NULL) {
	key = Tcl_GetHashKey(&serverTable, hPtr);
	if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
	    Tcl_AppendElement(interp, key);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


static int
AddStat(Tcl_Interp *interp, char *var, char *name, unsigned long val)
{
    char buf[20];

    sprintf(buf, "%lu", val);
    if (Tcl_SetVar2(interp, var, name, buf, TCL_LEAVE_ERR_MSG) == NULL) {
	return 0;
    }
    return 1;
}


static void
AddElem(Tcl_DString *dsPtr, char *name, unsigned long val)
{
    char buf[20];

    sprintf(buf, "%lu", val);
    Tcl_DStringAppendElement(dsPtr, name);
    Tcl_DStringAppendElement(dsPtr, buf);
}


static int
StatsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Server *sPtr;
    Client *cPtr;
    char *var;
    int status;
    Tcl_DString ds;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " server varName\"", NULL);
	return TCL_ERROR;
    }
    hPtr = Tcl_FindHashEntry(&serverTable, argv[1]);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such server: ", argv[1], NULL);
	return TCL_ERROR;
    }
    var = argv[2];
    sPtr = Tcl_GetHashValue(hPtr);
    status = TCL_ERROR;
    Tcl_DStringInit(&ds);
    do {
	if (!AddStat(interp, var, "nicb", sPtr->stats.nicb)) break;
	if (!AddStat(interp, var, "ndcb", sPtr->stats.ndcb)) break;
	if (!AddStat(interp, var, "nrcb", sPtr->stats.nrcb)) break;
	if (!AddStat(interp, var, "nscb", sPtr->stats.nscb)) break;
	if (!AddStat(interp, var, "nrecv", sPtr->stats.nrecv)) break;
	if (!AddStat(interp, var, "nsend", sPtr->stats.nsend)) break;
	cPtr = sPtr->firstClientPtr;
	while (cPtr != NULL) {
	    Tcl_DStringStartSublist(&ds);
	    Tcl_DStringAppendElement(&ds, "name");
	    Tcl_DStringAppendElement(&ds, cPtr->name);
	    Tcl_DStringAppendElement(&ds, "host");
	    Tcl_DStringAppendElement(&ds, cPtr->host);
	    AddElem(&ds, "port", (unsigned long)cPtr->port);
	    Tcl_DStringAppendElement(&ds, "state");
	    switch (cPtr->state) {
	    case Connecting:
	    	Tcl_DStringAppendElement(&ds, "connecting");
		break;
	    case RecvData:
	    	Tcl_DStringAppendElement(&ds, "recvdata");
		break;
	    case SendData:
	    	Tcl_DStringAppendElement(&ds, "senddata");
		break;
	    default:
	    	Tcl_DStringAppendElement(&ds, "unknown");
		break;
	    }
	    AddElem(&ds, "ctime", (unsigned long) cPtr->ctime);
	    AddElem(&ds, "mtime", (unsigned long) cPtr->mtime);
	    AddElem(&ds, "nsend", cPtr->stats.nsend);
	    AddElem(&ds, "nrecv", cPtr->stats.nrecv);
	    AddElem(&ds, "nicb", cPtr->stats.nicb);
	    AddElem(&ds, "ndcb", cPtr->stats.ndcb);
	    AddElem(&ds, "nrcb", cPtr->stats.nrcb);
	    AddElem(&ds, "nscb", cPtr->stats.nscb);
	    Tcl_DStringEndSublist(&ds);
	    cPtr = cPtr->nextClientPtr;
	}
	if (Tcl_SetVar2(interp, var, "clients", ds.string, TCL_LEAVE_ERR_MSG) == NULL) {
	    break;
	}
	status = TCL_OK;
    } while (0);
    Tcl_DStringFree(&ds);
    return status;
}
