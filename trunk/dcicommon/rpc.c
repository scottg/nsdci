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
 * Definition of RPC header sent from client to server before data.
 */
 
typedef struct RpcMsg {
    uint32_t	cmd;
    uint32_t	len;
} RpcMsg;

/*
 * The following structure defines an RPC server.
 */
 
typedef struct Rps {
    Dci_RpcProc *proc;	    	    /* Callback proc. */
    void *arg;	    	    	    /* Callback data. */
    char name[DCI_RPCNAMESIZE+1];   /* Server name. */
    char handshake[DCI_RPCNAMESIZE+1]; /* Connect handshake name. */
} Rps;

/*
 * The following structure maintains state for a server
 * connection to a client.
 */
 
typedef struct CData {
    RpcMsg rpcmsg;  	    /* Message header. */
    Ns_DString in;  	    /* Input buffer. */
    Ns_DString out; 	    /* Output response. */
    struct iovec iov[2];    /* Scatter/gather I/O context. */
} CData;

/*
 * Client related data structures.
 */
 
/*
 * The following structure defines context for the per-virtual
 * server RPC listener.
 */
 
typedef struct Listener {
    SOCKET  	sock;	    /* Listening socket. */
    int     	trigger[2]; /* Trigger to signal shutdown. */
    int     	started;    /* Flag to indicate startup complete. */
    int     	debug;	    /* Flag for verbose debug mode. */
    int     	startwait;  /* Time to wait for connections at startup. */
    Ns_Mutex	lock;	    /* Lock for stop flag. */
    Ns_Cond 	cond;	    /* Condition for stop flag. */
    Ns_Thread	thread;     /* Listern thread to wait at shutdown. */
    Tcl_HashTable clients;  /* Table of client end points. */
} Listener;
    
struct Conn;

/*
 * The following structure defines various I/O stats.
 */
 
typedef struct Stats {
    unsigned int  nrequest;  /* # of request attempts. */
    unsigned long nrecv;     /* # of bytes received. */
    unsigned long nsend;     /* # of bytes sent. */
    unsigned long nrecvwait; /* # of I/O waits before receive. */
    unsigned long nsendwait; /* # of I/O waits before sends. */
    unsigned long tmin;
    unsigned long tmax;
    ns_uint64     ttot;
} Stats;

/*
 * The following structure defines an RPC client end point.
 * One or more servers will connect to this end point.
 */

typedef struct Rpc {
    char *name;     	    /* Descriptive and handshake name. */    	
    struct Conn *firstPtr;  /* First connection. */
    struct Conn *lastPtr;   /* Last connection. */
    Listener *listenerPtr;  /* Listener for new connections. */
    char *host;             /* HTTP Host header. */
    char *addr;             /* HTTP address. */
    int   port;             /* HTTP port. */
    int   httpKeepAlive;    /* HTTP connections keep-alive flag */
    int   httpAvail;        /* Limit on number of HTTP connections */
    int	  httpmaxhdr;	    /* Limit on HTTP header size. */
    char *httpMethod;	    /* HTTP method (default: DCIRPC). */
    int nsocks;     	    /* Total number of connections. */
    int stop;	    	    /* Stop service flag. */
    int timeout;    	    /* I/O and connection wait timeout. */
    int nextid;     	    /* Next unique connection id. */
    Ns_Mutex lock;  	    /* Lock for queue. */
    Ns_Cond cond;   	    /* Condition for queue. */
    unsigned int nsocktimeout; /* # of sock wait timeouts. */
    unsigned int nsockerror;/* # of sock I/O errors. */
    unsigned int nreqtimeout; /* # of request timeouts. */
    unsigned int nnoconns;  /* # of requests with no conns avail. */
    unsigned int nsockwait; /* # of condition waits for avail sock. */
    Stats stats;   	    /* Total stats for all requests. */
} Rpc;

/*
 * The following structure defines a single connection to an
 * RPC end point.
 */
 
typedef struct Conn {
    struct Conn *nextPtr;   /* Next in FIFO list of socks. */
    struct Rpc  *rpcPtr;    /* Pointer to RPC end point. */
    int connid;     	    /* Unique connection id. */
    Ns_Time atime;   	    /* Connection accept time. */
    Ns_Time mtime;   	    /* Connection last use time. */
    Stats stats;    	    /* Stats for this connection. */
    Stats rstats;     	    /* Stats for current request. */
    struct sockaddr_in sa;  /* Socket address. */
    int sock;	    	    /* Underlying socket. */
    int http;		    /* Flag indicating HTTP socket. */
    int errnum;		    /* Saved I/O errno. */
    Dci_RpcJob *jobPtr;	    /* Current job context. */
    int len;		    /* Remaining data to send or receive. */
    char *body;		    /* Pointer to start of response. */
    struct msghdr msg;	    /* sendmsg/recvmsg struct. */
    struct iovec iov[2];    /* sendmsg/recvmsg iovec. */
    Ns_DString buf;	    /* Multiple use connection buffer. */
} Conn;

/*
 * The following structure defines Http Parsing results.
 */

typedef struct Parse {
    int result;             /* DciRpc result status code */
    int length;             /* Content length */
    int keep;               /* Keep alive flag */
} Parse;


static Conn *RpcGet(Dci_Rpc *rpc);
static void RpcPut(Conn *connPtr);
static int RpcSend(Conn *connPtr);
static int RpcRecv(Conn *connPtr);
static void RpcUpdateStats(Stats *fromPtr, Stats *toPtr);
static void RpcLog(Conn *connPtr, char *msg);
static void RpcClose(Conn *connPtr, char *msg);
static Conn *RpcPop(Rpc *rpcPtr);
static void RpcPush(Conn *connPtr);
static Conn *RpcNewConn(Rpc *rpcPtr, int sock, int http);
static void RpcSetRecv(Conn *connPtr);
static void RpcSetSend(Conn *connPtr, Dci_RpcJob *jobPtr);
static int RpcParseHttp(char *hdrs, Parse *parsePtr);
static int GetRpcClient(ClientData arg, Tcl_Interp *interp, char *client,
			Rpc **rpcPtrPtr);
static Ns_ThreadProc RpcListenThread;
static Ns_Callback RpcStartListener;
static Ns_Callback RpcStopListener;
static void RpcAccept(Listener *listenerPtr);
static Ns_OpProc RpsRequest;

static Dci_ServerProc RpsProc;
static Tcl_CmdProc RpcStatsCmd;
static Tcl_CmdProc RpcSendCmd;
static Tcl_CmdProc RpcClientsCmd;
static Ns_TclInterpInitProc AddCmds;

static Tcl_HashTable listeners;
static char *method;

#define NSTATIC 20
#define HTTP_KEEP 2


/*
 *----------------------------------------------------------------------
 *
 * DciRpcInit --
 *
 *      Inititalization function for RPC.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *  	The RPC listen thread is scheduled to start if configured.
 *
 *----------------------------------------------------------------------
 */

int
DciRpcInit(char *server, char *module)
{
    char *path, *addr;
    int port, new, sock;
    Listener *listenerPtr;
    Tcl_HashEntry *hPtr;
    static int once = 0;

    Dci_LogIdent(module, rcsid);
    if (!once) {
    	Tcl_InitHashTable(&listeners, TCL_STRING_KEYS);
	once = 1;
    }
    path = Ns_ConfigGetPath(server, module, "rpc", NULL);
    addr = Ns_ConfigGetValue(path, "address");
    if (!Ns_ConfigGetInt(path, "port", &port)) {
	return NS_OK;
    }
    sock = Ns_SockListen(addr, port);
    if (sock < 0) {
        Ns_Log(Error, "rpc: could not listen on %s:%d",
		addr ? addr : "*", port);
    	return NS_ERROR;
    }

    Ns_Log(Notice, "rpc: listening on %s:%d",
        addr, port);

    hPtr = Tcl_CreateHashEntry(&listeners, server, &new);
    if (!new) {
    	Ns_Log(Error, "rpc: already initialized %s", server);
	return NS_ERROR;
    }
    listenerPtr = ns_calloc(1, sizeof(Listener));
    listenerPtr->sock = sock;
    if (ns_pipe(listenerPtr->trigger) != 0) {
	Ns_Fatal("rpc: ns_pipe() failed: %s", strerror(errno));
    }
    Tcl_InitHashTable(&listenerPtr->clients, TCL_STRING_KEYS);
    if (!Ns_ConfigGetInt(path, "debug", &listenerPtr->debug)) {
    	listenerPtr->debug = 0;
    }
    if (!Ns_ConfigGetInt(path, "startwait", &listenerPtr->startwait)) {
    	listenerPtr->startwait = 1;
    }
    Tcl_SetHashValue(hPtr, listenerPtr);
    Ns_RegisterAtPreStartup(RpcStartListener, listenerPtr);
    Ns_RegisterAtShutdown(RpcStopListener, listenerPtr);
    Ns_TclInitInterps(server, AddCmds, listenerPtr);
    return NS_OK;
}

static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Listener *listenerPtr = arg;
    
    Tcl_CreateCommand(interp, "rpc.stats", RpcStatsCmd, arg, NULL);
    Tcl_CreateCommand(interp, "rpc.clients", RpcClientsCmd, arg, NULL);
    Tcl_CreateCommand(interp, "rpc.send", RpcSendCmd, arg, NULL);
    Tcl_CreateCommand(interp, "rpc.debug", DciSetDebugCmd,
		      &listenerPtr->debug, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcName --
 *
 *      Format a simple class:instance RPC name.
 *
 * Results:
 *  	NS_OK if format ok, NS_ERROR on error.
 *
 * Side effects:
 *      Given buf of at least DCI_RPCNAMESIZE in length is modified.
 *
 *----------------------------------------------------------------------
 */

int
Dci_RpcName(char *class, char *instance, char *buf)
{
    if ((strlen(class) + strlen(instance) + 2) >= DCI_RPCNAMESIZE) {
	Ns_Log(Error, "rpc: invalid name: %s:%s", class, instance);
	return NS_ERROR;
    }
    sprintf(buf, "%s:%s", class, instance);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcCreateClient --
 *
 *      Creates an RPC end point.
 *
 * Results:
 *      Pointer to a Dci_Rpc struct or NULL on error.
 *
 * Side effects:
 *      Requests will be sent to RPC servers which connect with given
 *  	name.
 *
 *----------------------------------------------------------------------
 */

Dci_Rpc *
Dci_RpcCreateClient(char *server, char *module, char *name, int timeout)
{
    Rpc *rpcPtr;
    Tcl_HashEntry *hPtr;
    int new;
    Listener *listenerPtr;
    char *path;

    if (strlen(name) >= DCI_RPCNAMESIZE) {
        Ns_Log(Error, "rpc: invalid name: %s", name);
        return NULL;
    }
    hPtr = Tcl_FindHashEntry(&listeners, server);
    if (hPtr == NULL) {
        Ns_Log(Error, "rpc: not listening on %s", server);
        return NULL;
    }
    listenerPtr = Tcl_GetHashValue(hPtr);
    hPtr = Tcl_CreateHashEntry(&listenerPtr->clients, name, &new);
    if (!new) {
        Ns_Log(Error, "rpc: already exists: %s", name);
        return NULL;
    }
    rpcPtr = ns_calloc(1, sizeof(Rpc));
    rpcPtr->listenerPtr = listenerPtr;
    rpcPtr->name = Tcl_GetHashKey(&listenerPtr->clients, hPtr);
    rpcPtr->timeout = timeout;
    Ns_MutexInit(&rpcPtr->lock);
    Ns_MutexSetName2(&rpcPtr->lock, "dci:rpc", rpcPtr->name);
    Tcl_SetHashValue(hPtr, rpcPtr);
    path = Ns_ConfigPath(server, module, "rpc/client", name, NULL);
    if (!Ns_ConfigGetInt(path, "port", &rpcPtr->port)) {
        rpcPtr->port = 80;
    }
    rpcPtr->addr = Ns_ConfigGet(path, "address");
    rpcPtr->host = Ns_ConfigGet(path, "host");
    if (rpcPtr->host == NULL) {
        rpcPtr->host = rpcPtr->addr;
    }
    if (!Ns_ConfigGetBool(path, "httpkeepalive", &rpcPtr->httpKeepAlive)) {
        rpcPtr->httpKeepAlive = 0;
    }
    if (!Ns_ConfigGetInt(path, "httpmaxheader", &rpcPtr->httpmaxhdr)) {
        rpcPtr->httpmaxhdr = 2*1024;	/* NB: 2k max header. */
    }
    rpcPtr->httpMethod = Ns_ConfigGet(path, "httpmethod");
    if (rpcPtr->httpMethod == NULL) {
	rpcPtr->httpMethod = "DCIRPC";
    }
    if (rpcPtr->host != NULL) {
        if (!Ns_ConfigGetInt(path, "httpnumconnections", &rpcPtr->httpAvail)) {
            rpcPtr->httpAvail = 1;
        }
        Ns_Log(Notice, "rpc[%s]: sending to http://%s:%d/%s (HTTP)", name, rpcPtr->addr, rpcPtr->port, name);
    } else {
        rpcPtr->httpAvail = 0;
        Ns_Log(Notice, "rpc[%s]: sending to %d (DCIRPC)", name, rpcPtr->port);
        
    }
    return (Dci_Rpc *) rpcPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcCreateServer --
 *
 *      Creates an RPC server.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *  	A new server thread will connect to the given clients.
 *
 *----------------------------------------------------------------------
 */

int
Dci_RpcCreateServer(char *server, char *module, char *name, char *handshake,
		    Ns_Set *clients, Dci_RpcProc *proc, void *arg)
{
    Rps *rpsPtr;
    Ns_DString ds;
    char *path, *method;
    int i;
    
    if (name == NULL || strlen(name) >= DCI_RPCNAMESIZE) {
	Ns_Log(Error, "rps: invalid name: %s", name);
	return NS_ERROR;
    }
    if (handshake == NULL) {
    	handshake = name;
    }
    if (strlen(handshake) >= DCI_RPCNAMESIZE) {
	Ns_Log(Error, "rps: invalid handshake: %s", handshake);
	return NS_ERROR;
    }
    path = Ns_ConfigGetPath(server, module, "rpc/server", name, NULL);
    rpsPtr = ns_calloc(1, sizeof(Rps));
    strcpy(rpsPtr->name, name);
    strcpy(rpsPtr->handshake, handshake);
    rpsPtr->arg = arg;
    rpsPtr->proc = proc;
    Ns_DStringInit(&ds);
    if (!Ns_ConfigGetBool(path, "http", &i) || i) {
	method = Ns_ConfigGet(path, "method");
	if (method == NULL) {
	    method = "DCIRPC";
	}
    	Ns_DStringVarAppend(&ds, "/", name, NULL);
    	Ns_RegisterRequest(server, method, ds.string, RpsRequest, NULL,
		           rpsPtr, 0);
	Ns_Log(Notice, "rpc[%s]: registered %s (HTTP)", name, ds.string);
    } 
    if (clients != NULL && Ns_SetSize(clients) > 0) {
    	Ns_DStringTrunc(&ds, 0);
    	Ns_DStringVarAppend(&ds, "rps:", name, NULL);
    	if (Dci_CreateServer(ds.string, rpsPtr, clients, RpsProc) != NS_OK) {
            Ns_Log(Warning, "rps: no valid clients defined");
	}
    }
    Ns_DStringFree(&ds);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcSend, Dci_RpcTimedSend --
 *
 *  	Send a request to an RPC server.
 *
 * Results:
 *      RPC_OK:		Request and response OK.
 *	RPC_NOCONN:	No connection available.
 *	RPC_TIMEOUT:	Timeout waiting for server response.
 *	RPC_ERROR:	Error receiving response or server error.
 *
 * Side effects:
 *  	Request will be sent to next available server and response
 *  	will be in given outPtr dstring.
 *
 *----------------------------------------------------------------------
 */

int
Dci_RpcSend(Dci_Rpc *rpc, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    Rpc *rpcPtr = (Rpc *) rpc;

    /*
     * Call Dci_RpcTimedSend with the response timeout equal to the
     * default socket wait and I/O timeout.
     */
     
    return Dci_RpcTimedSend(rpc, cmd, inPtr, outPtr, rpcPtr->timeout);
}

int
Dci_RpcTimedSend(Dci_Rpc *rpc, int cmd, Ns_DString *inPtr, Ns_DString *outPtr,
		int timeout)
{
    Dci_RpcJob job;

    /* 
     * Call Dci_RpcRun with a single job.
     */

    job.rpc = rpc;
    job.cmd = cmd;
    job.inPtr = inPtr;
    job.outPtr = outPtr;
    Dci_RpcRun(&job, 1, timeout, 0);

    /*
     * Return old-style result which confuses RPC protocol errors
     * with server response codes.  In practice, RPC servers bundle
     * their extended status in the result data and the result code
     * is one of RPC_OK, TCL_OK, or NS_OK, all of which are defined
     * as zero.
     */

    switch (job.error) {
    case RPC_OK:
	job.error = job.result;
	break;
    case RPC_ENOCONN:
	job.error = RPC_NOCONN;
	break;
    case RPC_ESEND:
    case RPC_ERECV:
    case RPC_EHTTPSIZE:
    case RPC_EHTTPPARSE:
	job.error = RPC_ERROR;
	break;
    case RPC_ESENDWAIT:
    case RPC_ERECVWAIT:
	job.error = RPC_TIMEOUT;
	break;
    }
    return job.error;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcRun --
 *
 *  	Run one or more RPC requests.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	RPC requests for each job will be sent to the given server(s).
 *	Response data and status will be returned in each job struct.
 *
 *----------------------------------------------------------------------
 */

void
Dci_RpcRun(Dci_RpcJob *jobs, int njob, int reqtimeout, int flags)
{
    Conn *connPtr, **conns, *staticConns[NSTATIC];
    struct pollfd *pfds, staticPfd[NSTATIC];
    int i, ms, nbusy, iotimeout;
    Ns_Time now, timeout, diff;
    short revents;
    
    /*
     * Allocate job control arrays.
     */

    if (njob > NSTATIC) {
	pfds = ns_malloc(sizeof(struct pollfd) * njob);
	conns = ns_malloc(sizeof(Conn *) * njob);
    } else {
	pfds = staticPfd;
	conns = staticConns;
    }

    /*
     * Get and setup each conn.
     */

    Ns_GetTime(&now);
    nbusy = 0;
    iotimeout = 0;
    for (i = 0; i < njob; ++i) {
	pfds[i].revents = 0;
	jobs[i].result = 0;
	jobs[i].tstart = now;
	connPtr = conns[i] = RpcGet(jobs[i].rpc);
	if (connPtr == NULL) {
	    jobs[i].error = RPC_ENOCONN;
	    pfds[i].fd = -1;
	    pfds[i].events = 0;
	} else {
	    /* NB: First possible error is send wait timeout. */
	    RpcSetSend(connPtr, &jobs[i]);
	    jobs[i].error = RPC_ESENDWAIT;
	    pfds[i].fd = connPtr->sock;
	    pfds[i].events = POLLOUT;
	    ++nbusy;
	    if (iotimeout < connPtr->rpcPtr->timeout) {
	    	iotimeout = connPtr->rpcPtr->timeout;
	    }
	}
    }
    if (nbusy != njob /*&& !(flags & RPC_RUNANY)*/) {
	/* NB: All jobs or no jobs must be attempted. */
	for (i = 0; i < njob; ++i) {
	    if (conns[i] != NULL) {
		jobs[i].error = RPC_ENOCONN;
		RpcPut(conns[i]);
	    }
	}
	goto done;
    }

    /*
     * Run the jobs.
     */

    /* NB: Need to be smarter about timeout. */
    Ns_GetTime(&now);
    timeout = now;
    if (reqtimeout < 0) {
	reqtimeout = iotimeout;
    }
    Ns_IncrTime(&timeout, reqtimeout + (iotimeout * 2), 0);
    while (nbusy) {
	if (Ns_DiffTime(&timeout, &now, &diff) < 0) {
	    break;
	}
	ms = diff.sec * 1000 + diff.usec / 1000;
	do {
	    i = poll(pfds, (unsigned int) njob, ms);
	} while (i < 0 && errno == EINTR);
	if (i < 0) {
	    Ns_Fatal("rpc: poll failed: %s", strerror(errno));
	}
	Ns_GetTime(&now);
	if (i == 0) {
	    break;
	}
    	for (i = 0; i < njob; ++i) {
	    if (pfds[i].events != 0) {
	    	connPtr = conns[i];
		revents = pfds[i].revents;
		pfds[i].revents = 0;
	    	if (revents & POLLOUT) {
	    	    if (jobs[i].error == RPC_ESENDWAIT) {
		    	jobs[i].tsend = now;
		    }
		    connPtr->jobPtr->error = RpcSend(connPtr);
		} else if (revents & POLLIN) {
	    	    if (jobs[i].error == RPC_ETIMEOUT) {
		    	jobs[i].trecv = now;
		    }
		    connPtr->jobPtr->error = RpcRecv(connPtr);
		} else {
		    continue;
		}
		switch (connPtr->jobPtr->error) {
		case RPC_ESENDWAIT:
		case RPC_ERECVWAIT:
		    /* NB: More I/O pending. */
		    break;
		case RPC_ETIMEOUT:
		    /* NB: Send complete, wait for response. */
		    jobs[i].twait = now;
		    RpcSetRecv(connPtr);
		    pfds[i].events = POLLIN;
		    break;
		case RPC_OK:
		case RPC_ESEND:
		case RPC_ERECV:
		case RPC_EHTTPSIZE:
		case RPC_EHTTPPARSE:
		    /* NB: Request complete or failed, job done. */
		    pfds[i].events = 0;
		    jobs[i].tdone = now;
		    --nbusy;
		    break;
		default:
		    Ns_Fatal("Dci_RpcRun: invalid job I/O");
		}
	    }
	}
    }
    for (i = 0; i < njob; ++i) {
	connPtr = conns[i];
	if (jobs[i].error == RPC_OK) {
	    Ns_DiffTime(&jobs[i].tdone, &jobs[i].tstart, &diff);
	    connPtr->rstats.ttot = diff.sec * 1000 + diff.usec / 1000;
	    connPtr->mtime = now;
	    RpcPut(connPtr);
	    connPtr = NULL;
	} else if (connPtr != NULL) {
    	    Rpc *rpcPtr = connPtr->rpcPtr;
    	    char *msg;
    	    switch (connPtr->jobPtr->error) {
    	    case RPC_ESEND:
		msg = "send";
		goto fmtmsg;
		break;
    	    case RPC_ERECV:
		msg = "recv";
fmtmsg:
		Ns_DStringTrunc(&connPtr->buf, 0);
		Ns_DStringVarAppend(&connPtr->buf, msg, " failed: ",
			strerror(connPtr->errnum), NULL);
		msg = connPtr->buf.string;
		break;
     	    case RPC_ETIMEOUT:
		msg = "timeout waiting for response";
		break;
     	    case RPC_ESENDWAIT:
		msg = "timeout waiting for send";
		break;
     	    case RPC_ERECVWAIT:
		msg = "timeout waiting for receive";
		break;
     	    case RPC_EHTTPSIZE:
		msg = "http response error (oversized)";
		break;
     	    case RPC_EHTTPPARSE:
		msg = "http response error (invalid rpc result)";
		break;
     	    default:
		Ns_Fatal("Dci_RpcRun: invalid job completion");
	    	break;
	    }
    	    Ns_MutexLock(&rpcPtr->lock);
    	    if (connPtr->jobPtr->error == RPC_ETIMEOUT) {
    		++rpcPtr->nreqtimeout;
    	    } else {
    		++rpcPtr->nsockerror;
    	    }
    	    --rpcPtr->nsocks;
    	    if (connPtr->http) {
        	++rpcPtr->httpAvail;
    	    }
    	    Ns_CondBroadcast(&rpcPtr->cond);
    	    Ns_MutexUnlock(&rpcPtr->lock);
    	    RpcClose(connPtr, msg);
	}
    }
done:
    if (pfds != staticPfd) {
	ns_free(pfds);
	ns_free(conns);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_RpcTclError --
 *
 *  	Set a formatted error message and id in Tcl.
 *
 * Results:
 *  	Static error message string.
 *
 * Side effects:
 *  	Error info and code are set with DCIRPC class in given interp.
 *
 *----------------------------------------------------------------------
 */

char *
Dci_RpcTclError(Tcl_Interp *interp, int err)
{
    char *id, *msg;

    /* FIX: Remove assumption positive error is valid errno. */
    if (err > 0) {
	int save = Tcl_GetErrno();
	Tcl_SetErrno(err);
	msg = Tcl_PosixError(interp);
	Tcl_SetErrno(save);
	return msg;
    }

    switch (err) {
    case RPC_OK:
    	id = "RPC_OK";
	msg = "no error";
	break;
    case RPC_ENOCONN:
        id = "RPC_NOCONN";
        msg = "no connection available";
	break;
    case RPC_ETIMEOUT:
        id = "RPC_TIMEOUT";
        msg = "timeout waiting for response";
	break;
    case RPC_ESEND:
    case RPC_ERECV:
    	id = "RPC_ERROR";
    	msg = "request I/O error";
	break;
    case RPC_EHTTPSIZE:
    case RPC_EHTTPPARSE:
    	id = "RPC_ERROR";
    	msg = "server response error";
	break;
    default:
        id = "unknown";
        msg = "unknown DCIRPC error";
	break;
    }
    Tcl_SetErrorCode(interp, "DCIRPC", id, msg, (char *) NULL);    
    return msg;
}


/*
 *----------------------------------------------------------------------
 *
 * RpcUpdateStats --
 *
 *      Update Stats struct with temporary request counts.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Counts in toPtr are updated.
 *
 *----------------------------------------------------------------------
 */

static void
RpcUpdateStats(Stats *fromPtr, Stats *toPtr)
{
    int ms = fromPtr->ttot;

    ++toPtr->nrequest;
    toPtr->nrecv += fromPtr->nrecv;
    toPtr->nsend += fromPtr->nsend;
    toPtr->nrecvwait += fromPtr->nrecvwait;
    toPtr->nsendwait += fromPtr->nsendwait;
    toPtr->ttot += ms;
    if (toPtr->tmin == 0 || toPtr->tmin > ms) {
	toPtr->tmin = ms;
    }
    if (toPtr->tmax < ms) {
	toPtr->tmax = ms;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RpcGet, RpcPut --
 *
 *      Get/Put an available connection.
 *
 * Results:
 *      Pointer to Conn or NULL on non available.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static Conn *
RpcGet(Dci_Rpc *rpc)
{
    Rpc *rpcPtr = (Rpc *) rpc;
    Ns_Time timeout;
    Conn *connPtr;
    struct pollfd pfd;
    int sock, status;
    int httpConnDrop;
    
    connPtr = NULL;
    status = NS_OK;
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, rpcPtr->timeout, 0);
    Ns_MutexLock(&rpcPtr->lock);
    do {
    	while ((connPtr = RpcPop(rpcPtr)) == NULL
		&& rpcPtr->nsocks > 0 && rpcPtr->httpAvail == 0
		&& status == NS_OK) {
	    status = Ns_CondTimedWait(&rpcPtr->cond, &rpcPtr->lock, &timeout);
            ++rpcPtr->nsockwait;
	}
    	if (connPtr != NULL) {
	    Ns_MutexUnlock(&rpcPtr->lock);
	    pfd.events = POLLIN;
	    pfd.fd = connPtr->sock;
            httpConnDrop = 0;
	    if (poll(&pfd, 1, 0)) {
                if (connPtr->http) {
                    httpConnDrop = 1;
                }
    	    	RpcClose(connPtr, "server dropped");
	    	connPtr = NULL;
	    }
	    Ns_MutexLock(&rpcPtr->lock);
	    if (connPtr == NULL) {
	    	--rpcPtr->nsocks;
                if (httpConnDrop) {
                    ++rpcPtr->httpAvail;
                }
	    	Ns_CondBroadcast(&rpcPtr->cond);
	    }
	}
    } while (connPtr == NULL && rpcPtr->nsocks > 0 && rpcPtr->httpAvail == 0 && status == NS_OK);
    if (connPtr == NULL && rpcPtr->host != NULL) {
    	Ns_MutexUnlock(&rpcPtr->lock);
    	sock = Ns_SockAsyncConnect(rpcPtr->addr, rpcPtr->port);
    	Ns_MutexLock(&rpcPtr->lock);
	if (sock != -1) {
	    connPtr = RpcNewConn(rpcPtr, sock, 1);
	    ++rpcPtr->nsocks;
            --rpcPtr->httpAvail;
	}
    }
    if (connPtr == NULL) {
    	if (rpcPtr->nsocks == 0) {
	    ++rpcPtr->nnoconns;
	} else if (status != NS_OK) {
	    ++rpcPtr->nsocktimeout;
	}
    }
    Ns_MutexUnlock(&rpcPtr->lock);
    if (connPtr != NULL) {
    	memset(&connPtr->rstats, 0, sizeof(Stats));
    }
    return connPtr;
}

static void
RpcPut(Conn *connPtr)
{
    Rpc *rpcPtr = connPtr->rpcPtr;
    int doClose = 0;

    Ns_DStringFree(&connPtr->buf);
    RpcUpdateStats(&connPtr->rstats, &connPtr->stats);
    Ns_MutexLock(&rpcPtr->lock);
    RpcUpdateStats(&connPtr->rstats, &rpcPtr->stats);
    if (!connPtr->http || (connPtr->http & HTTP_KEEP)) {
	RpcPush(connPtr);
    } else {
        --rpcPtr->nsocks;
        ++rpcPtr->httpAvail;
        doClose = 1;
    }
    Ns_CondBroadcast(&rpcPtr->cond);
    Ns_MutexUnlock(&rpcPtr->lock);
    if (doClose) {
	RpcClose(connPtr, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RpcSend, RpcRecv --
 *
 *      Send or receive data on a connection while running a job.
 *
 * Results:
 *      
 *
 * Side effects:
 *  	Will update the sendmsg/recvmsg iov as needed.
 *
 *----------------------------------------------------------------------
 */

static int
RpcSend(Conn *connPtr)
{
    int n;

    n = sendmsg(connPtr->sock, &connPtr->msg, 0);
    if (n < 0) {
	connPtr->errnum = errno;
	return RPC_ESEND;
    }
    connPtr->rstats.nsend += n;
    connPtr->len -= n;
    if (connPtr->len > 0) {
	Dci_UpdateIov(connPtr->iov, 2, n);
	++connPtr->rstats.nsendwait;
	return RPC_ESENDWAIT;
    }
    return RPC_ETIMEOUT;
}

static int
RpcRecv(Conn *connPtr)
{
    int n, len;
    RpcMsg *msgPtr;
    Ns_DString *outPtr;
    Parse parse;
    char *base;

    n = recvmsg(connPtr->sock, &connPtr->msg, 0);
    if (n <= 0) {
	connPtr->errnum = (n ? errno : 0);
	return RPC_ERECV;
    }
    connPtr->rstats.nrecv += n;

    /*
     * See if enough data has arrive to determine and update the length.
     */

    if (connPtr->body == NULL) {
    	if (connPtr->http) {
	    /*
	     * Scan current input for valid HTTP headers.
	     */

	    base = connPtr->iov[0].iov_base;
	    base[n] = '\0';
	    connPtr->body = strstr(connPtr->buf.string, "\r\n\r\n");
	    if (connPtr->body == NULL) {
		    if (connPtr->len == n) {
			/* NB: HTTP header to large. */
		    	return RPC_EHTTPSIZE;
		    }
	    } else {
		*connPtr->body = '\0';
		connPtr->body += 4;
		n += (base - connPtr->body);
		if (!RpcParseHttp(connPtr->buf.string, &parse)) {
		    /* NB: Invalid RPC HTTP response. */
		    return RPC_EHTTPPARSE;
		}
		connPtr->len = len = parse.length;
		connPtr->jobPtr->result = parse.result;
		if (parse.keep) {
		    connPtr->http |= HTTP_KEEP;
		} else {
		    connPtr->http &= ~(HTTP_KEEP);
		}
		outPtr = connPtr->jobPtr->outPtr;
		if (outPtr == NULL) {
		    /* NB: Length expected to be zero in this case. */
		    outPtr = &connPtr->buf;
		} else {
		    Ns_DStringSetLength(outPtr, 0);
		    Ns_DStringNAppend(outPtr, connPtr->body, n);
		}
		Ns_DStringSetLength(outPtr, len);
		connPtr->iov[0].iov_base = outPtr->string;
		connPtr->iov[0].iov_len = outPtr->length;
	    }
	} else if (n >= connPtr->iov[0].iov_len) {
	    /*
	     * Get result and length from RpcMsg header.
	     */

	    msgPtr = (RpcMsg *) connPtr->buf.string;
	    connPtr->jobPtr->result = ntohl(msgPtr->cmd);
    	    len = ntohl(msgPtr->len);
	    outPtr = connPtr->jobPtr->outPtr;
	    if (outPtr == NULL) {
		/* NB: Length expected to be zero in this case. */
		outPtr = &connPtr->buf;
	    }
    	    Ns_DStringSetLength(outPtr, n - connPtr->iov[0].iov_len);
    	    Ns_DStringSetLength(outPtr, len);
    	    connPtr->iov[1].iov_base = outPtr->string;
    	    connPtr->iov[1].iov_len = len;
	    connPtr->body = outPtr->string;
	    connPtr->len = connPtr->iov[0].iov_len + connPtr->iov[1].iov_len;
	}
    }
    Dci_UpdateIov(connPtr->iov, 2, n);
    connPtr->len -= n;
    return (connPtr->len ? RPC_ERECVWAIT : RPC_OK);
}


/*
 *----------------------------------------------------------------------
 *
 * RpcListenThread --
 *
 *      Thread to listen forever for RPC server connects.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Client connections will be accepted.
 *
 *----------------------------------------------------------------------
 */

static void
RpcListenThread(void *arg)
{
    Listener *listenerPtr = arg;
    int n, timeout;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Rpc *rpcPtr;
    Conn *connPtr;
    struct pollfd pfds[2];

    Ns_ThreadSetName("-rpclisten-");

    /*
     * Wait for any inbound connections or the shutdown trigger.
     * At startup, keep spinning until any pending connections
     * are excepted before signaling startup ready.
     */

    pfds[0].fd = listenerPtr->trigger[0];
    pfds[1].fd = listenerPtr->sock;
    pfds[0].events = pfds[1].events = POLLIN;
    timeout = listenerPtr->startwait * 1000;
    do {
    	pfds[0].revents = pfds[1].revents = 0;
	do {
	    n = poll(pfds, 2, timeout);
	} while (n < 0 && errno == EINTR);
	if (n < 0) {
	    Ns_Fatal("poll() failed: %s", strerror(errno));
	}
    	if (n == 0) {
	    Ns_MutexLock(&listenerPtr->lock);
	    listenerPtr->started = 1;
	    Ns_CondSignal(&listenerPtr->cond);
	    Ns_MutexUnlock(&listenerPtr->lock);
	    timeout = -1;   /* Infinite wait after startup. */
	} else if (pfds[1].revents & POLLIN) {
	    RpcAccept(listenerPtr);
	}
    } while (!(pfds[0].revents & POLLIN));

    /* 
     * Close the listen socket and shutdown all client connections.
     */
     
    close(listenerPtr->sock);
    hPtr = Tcl_FirstHashEntry(&listenerPtr->clients, &search);
    while (hPtr != NULL) {
	rpcPtr = Tcl_GetHashValue(hPtr);
    	Ns_Log(Notice, "%s: shutdown pending", rpcPtr->name);
    	Ns_MutexLock(&rpcPtr->lock);
    	rpcPtr->stop = 1;
	while (rpcPtr->nsocks > 0) {
	    while ((connPtr = RpcPop(rpcPtr)) == NULL && rpcPtr->nsocks > 0) {
	    	Ns_CondWait(&rpcPtr->cond, &rpcPtr->lock);
	    }
	    if (connPtr != NULL) {
    	    	RpcClose(connPtr, "shutdown pending");
		--rpcPtr->nsocks;
	    }
	}
    	Ns_MutexUnlock(&rpcPtr->lock);
    	Ns_Log(Notice, "%s: shutdown complete", rpcPtr->name);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&listenerPtr->clients);
}


/*
 *----------------------------------------------------------------------
 *
 * RpcStartListener --
 *
 *      Pre-startup callback to create and wait for the listen thread
 *  	to startup.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	New listen thread created.
 *
 *----------------------------------------------------------------------
 */

static void
RpcStartListener(void *arg)
{
    Listener *listenerPtr = arg;
    Ns_Time timeout;
    int status;

    Ns_Log(Notice, "rpc: starting listener thread");
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, 1, 0);
    Ns_ThreadCreate(RpcListenThread, arg, 0, &listenerPtr->thread);
    Ns_MutexLock(&listenerPtr->lock);
    status = NS_OK;
    while (!listenerPtr->started && status == NS_OK) {
	status = Ns_CondTimedWait(&listenerPtr->cond, &listenerPtr->lock, &timeout);
    }
    Ns_MutexUnlock(&listenerPtr->lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "rpc: timeout waiting for accept of pending connections");
    } else {
	Ns_Log(Notice, "rpc: listen thread started");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RpcStopListener --
 *
 *      Shutdown callback to trigger a listen thread to stop and exit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *  	Listen thread will close all connections and exit.
 *
 *----------------------------------------------------------------------
 */

static void
RpcStopListener(void *arg)
{
    Listener *listenerPtr = arg;
    
    if (write(listenerPtr->trigger[1], "", 1) != 1) {
	Ns_Fatal("rpc: trigger write() failed: %s", strerror(errno));
    }
    Ns_ThreadJoin(&listenerPtr->thread, NULL);
    close(listenerPtr->trigger[0]);
    close(listenerPtr->trigger[1]);
}
    

/*
 *----------------------------------------------------------------------
 *
 * RpcAccept --
 *
 *      Accept and handshake with a new connection.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	New connection is available for given RPC end point and all
 *  	other connections are checked for possible server drop.
 *
 *----------------------------------------------------------------------
 */

static void
RpcAccept(Listener *listenerPtr)
{
    struct pollfd *pfds;
    Conn *connPtr, *firstPtr, *nextPtr;
    Tcl_HashEntry *hPtr;
    Rpc *rpcPtr;
    int n, sock, len, nfds, ndrop;
    char *ptr, handshake[DCI_RPCNAMESIZE+1];
    Ns_DString ds;

    /*
     * Accept and setup new server socket.
     */

    sock = Ns_SockAccept(listenerPtr->sock, NULL, NULL);
    if (sock < 0) {
	Ns_Log(Error, "rpc: accept() failed: %s", strerror(errno));
	return;
    }
    Ns_SockSetNonBlocking(sock);
    Dci_SockOpts(sock, DCI_SOCK_CLIENT);

    /*
     * Read the handshake to determine th correct login.
     */
     
    len = DCI_RPCNAMESIZE;
    ptr = handshake;
    ptr[len] = '\0';
    while (len > 0) {
	n = Ns_SockRecv(sock, ptr, len, 1);
	if (n <= 0) {
	    Ns_Log(Error, "rpc: handshake recv() failed: %s", strerror(errno));
	    close(sock);
	    return;
	}
	ptr += n;
	len -= n;
    }
    hPtr = Tcl_FindHashEntry(&listenerPtr->clients, handshake);
    if (hPtr == NULL) {
   	Ns_Log(Error, "rpc: no such client: %s", handshake);
    	close(sock);
	return;
    }
    rpcPtr = Tcl_GetHashValue(hPtr);
    connPtr = RpcNewConn(rpcPtr, sock, 0);

    /*
     * Setup the new connection, add it to the list, and then
     * check all available connections for readability indicating
     * server drop.
     */

    Ns_MutexLock(&rpcPtr->lock);
    connPtr->connid = rpcPtr->nextid++;
    ++rpcPtr->nsocks;
    RpcPush(connPtr);
    RpcLog(connPtr, "accepted connection");

    /*
     * Check all connections for readability indicating server drop.
     */
     
    firstPtr = rpcPtr->firstPtr;
    rpcPtr->firstPtr = rpcPtr->lastPtr = NULL;
    nfds = rpcPtr->nsocks;
    Ns_MutexUnlock(&rpcPtr->lock);
    Ns_DStringInit(&ds);
    Ns_DStringSetLength(&ds, (int)(sizeof(struct pollfd) * nfds));
    pfds = (struct pollfd *) ds.string;
    nfds = 0;
    connPtr = firstPtr;
    while (connPtr != NULL) {
	pfds[nfds].events = POLLIN;
	pfds[nfds].revents = 0;
	pfds[nfds].fd = connPtr->sock;
    	++nfds;
	connPtr = connPtr->nextPtr;
    }
    do {
	n = poll(pfds, (size_t) nfds, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
	Ns_Fatal("rpc: poll() failed: %s", strerror(errno));
    }
    nfds = ndrop = 0;
    connPtr = firstPtr;
    firstPtr = NULL;
    while (connPtr != NULL) {
	nextPtr = connPtr->nextPtr;
	if (pfds[nfds].revents & POLLIN) {
	    RpcClose(connPtr, "server drop");
	    ++ndrop;
	} else {
	    connPtr->nextPtr = firstPtr;
	    firstPtr = connPtr;
	}
	connPtr = nextPtr;
	++nfds;
    }
    
    /*
     * Return valid connections back to the list.
     */
     
    Ns_MutexLock(&rpcPtr->lock);
    rpcPtr->nsocks -= ndrop;
    while ((connPtr = firstPtr) != NULL) {
	firstPtr = connPtr->nextPtr;
	RpcPush(connPtr);
    }
    Ns_CondBroadcast(&rpcPtr->cond);
    Ns_MutexUnlock(&rpcPtr->lock);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * RpcLog --
 *      Provides additional logging information for Rpc, in the 
 *      server log.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Log entry is created.
 *
 *----------------------------------------------------------------------
 */

static void
RpcLog(Conn *connPtr, char *msg)
{
    Ns_DString ds;
    char id[10];

    sprintf(id, "%d", connPtr->connid);
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "rpc[", connPtr->rpcPtr->name, ":",
    	    	    	id, "]: ", msg, NULL);
    DciLogPeer(connPtr->sock, ds.string);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * RpcPop, RpcPush --
 *
 *  	Pop/push a connection on the open list.
 *
 * Results:
 *      None (push) or pointer to connection (pop).
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static Conn *
RpcPop(Rpc *rpcPtr)
{
    Conn *connPtr;

    connPtr = rpcPtr->firstPtr;
    if (connPtr != NULL) {
	rpcPtr->firstPtr = connPtr->nextPtr;
	if (rpcPtr->lastPtr == connPtr) {
	    rpcPtr->lastPtr = NULL;
	}
	connPtr->nextPtr = NULL;
    }
    return connPtr;
}

static void
RpcPush(Conn *connPtr)
{
    Rpc *rpcPtr = connPtr->rpcPtr;
    
    connPtr->nextPtr = NULL;
    if (rpcPtr->firstPtr == NULL) {
	rpcPtr->firstPtr = connPtr;
    } else {
	rpcPtr->lastPtr->nextPtr = connPtr;
    }
    rpcPtr->lastPtr = connPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * RpcClose --
 *
 *      Close and free a connection, logging the reason as necessary.
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
RpcClose(Conn *connPtr, char *msg)
{
    if (msg != NULL) {
    	RpcLog(connPtr, msg);
    }
    close(connPtr->sock);
    Ns_DStringFree(&connPtr->buf);
    ns_free(connPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * RpsProc, RpsRequest --
 *
 *      These are the core Rpc server callbacks used to recieve incoming
 *      client messages and client connection drops. The client
 *      messages are then passed to the proc for further processing.
 *
 * Results:
 *  	NS_OK always.
 *
 * Side effects:
 *      Depends on given Rpc proc.
 *
 *----------------------------------------------------------------------
 */

static int
RpsProc(Dci_Client *client, void *arg, int why)
{
    Rps *rpsPtr;
    CData *dataPtr;
    int len, cmd, status;
    Ns_DString *inPtr, *outPtr;

    rpsPtr = arg;    
    dataPtr = Dci_ServerGetData(client);
    if (dataPtr == NULL) {
    	dataPtr = ns_calloc(1, sizeof(CData));
	Ns_DStringInit(&dataPtr->in);
	Ns_DStringInit(&dataPtr->out);
	Dci_ServerSetData(client, dataPtr);
    }

    inPtr = &dataPtr->in;
    outPtr = &dataPtr->out;
    switch (why) {
    case DCI_SERVER_RECV:
    	len = ntohl(dataPtr->rpcmsg.len);
	if (len != 0) {
	    dataPtr->rpcmsg.len = htonl(0);
	    Ns_DStringSetLength(inPtr, len);
	    Dci_ServerRecv(client, inPtr->string, inPtr->length);
	    return NS_OK;
	}
    	cmd = ntohl(dataPtr->rpcmsg.cmd);
	status = (*rpsPtr->proc)(rpsPtr->arg, cmd, inPtr, outPtr);
	len = outPtr->length;
    	dataPtr->rpcmsg.cmd = htonl(status);
	dataPtr->rpcmsg.len = htonl(len);
	dataPtr->iov[0].iov_base = (caddr_t) &dataPtr->rpcmsg;
	dataPtr->iov[0].iov_len = sizeof(dataPtr->rpcmsg);
	dataPtr->iov[1].iov_base = outPtr->string;
	dataPtr->iov[1].iov_len = len;
	Dci_ServerSendVec(client, dataPtr->iov, 2);
	break;

    case DCI_SERVER_INIT:
    case DCI_SERVER_SEND:
	if (why == DCI_SERVER_INIT) {
            Dci_ServerSend(client, rpsPtr->handshake, DCI_RPCNAMESIZE);
	} else {
            Dci_ServerRecv(client, (char *) &dataPtr->rpcmsg,
			   sizeof(dataPtr->rpcmsg));
	}
    	/* FALLTHROUGH */
    case DCI_SERVER_DROP:
	Ns_DStringTrunc(outPtr, 0);
    	Ns_DStringTrunc(inPtr, 0);
	break;
    }

    return NS_OK;
}

static int
RpsRequest(void *arg, Ns_Conn *conn)
{
    Rps *rpsPtr = arg;
    Ns_DString in, out;
    int cmd, res, status;
    char hdr[100];
    Ns_Set *hdrs;
    char *connectHdr;
    int keepFlag = 0;

    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    if (conn->request->urlc != 2) {
	return NS_ERROR;
    }
    if (Tcl_GetInt(NULL, conn->request->urlv[1], &cmd) != TCL_OK) {
	return NS_ERROR;
    }
    Ns_ConnCopyToDString(conn, (size_t)Ns_ConnContentLength(conn), &in);
    hdrs = Ns_ConnHeaders(conn);
    if (hdrs != NULL) {
        connectHdr = Ns_SetIGet(hdrs, "connection");
        if (connectHdr != NULL && STRIEQ(connectHdr, "keep-alive")) {
            keepFlag = 1;
        }
    }
    res = (*rpsPtr->proc)(rpsPtr->arg, cmd, &in, &out);
    sprintf(hdr, "%d", res);
#ifdef NS_CONN_KEEPALIVE
    /* pre-4.0 aolservers did not export this definition */
    if (keepFlag) {
        conn->flags |= NS_CONN_KEEPALIVE;
    }
#endif
    Ns_ConnSetHeaders(conn, "x-dcirpc-result", hdr);
    status = Ns_ConnReturnData(conn, 200, out.string, out.length, NULL);
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * RpcStatsCmd --
 *
 *  	Return various statistics for a given RPC.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Given variable, treated as an array, is updated with data.
 *
 *----------------------------------------------------------------------
 */

static int
AddStat(Tcl_Interp *interp, char *var, char *name, unsigned int val)
{
    char buf[20];

    sprintf(buf, "%u", val);
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
RpcStatsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_DString ds;
    Rpc *rpcPtr;
    Conn *connPtr;
    int status;
    unsigned long tavg;
    char *var;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " name varName\"", NULL);
	return TCL_ERROR;
    }
    if (GetRpcClient(arg, interp, argv[1], &rpcPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    var = argv[2];
    Tcl_DStringInit(&ds);
    Ns_MutexLock(&rpcPtr->lock);
    if (rpcPtr->stats.nrequest == 0) {
	tavg = 0;
    } else {
	tavg = (unsigned long) (rpcPtr->stats.ttot / rpcPtr->stats.nrequest);
    }
    status = TCL_ERROR;
    do {
    	if (!AddStat(interp, var, "nsocks", (unsigned int)rpcPtr->nsocks)) break;
    	if (!AddStat(interp, var, "nsockwait", rpcPtr->nsockwait)) break;
    	if (!AddStat(interp, var, "nsocktimeout", rpcPtr->nsocktimeout)) break;
    	if (!AddStat(interp, var, "nsockerror", rpcPtr->nsockerror)) break;
    	if (!AddStat(interp, var, "nnosocks", rpcPtr->nnoconns)) break;
    	if (!AddStat(interp, var, "nreqtimeout", rpcPtr->nreqtimeout)) break;
    	if (!AddStat(interp, var, "nrequest", rpcPtr->stats.nrequest)) break;
    	if (!AddStat(interp, var, "nrecv", rpcPtr->stats.nrecv)) break;
    	if (!AddStat(interp, var, "nsend", rpcPtr->stats.nsend)) break;
    	if (!AddStat(interp, var, "nrecvwait", rpcPtr->stats.nrecvwait)) break;
    	if (!AddStat(interp, var, "nsendwait", rpcPtr->stats.nsendwait)) break;
    	if (!AddStat(interp, var, "tmin", rpcPtr->stats.tmin)) break;
    	if (!AddStat(interp, var, "tmax", rpcPtr->stats.tmax)) break;
    	if (!AddStat(interp, var, "tavg", tavg)) break;
	connPtr = rpcPtr->firstPtr;
	while (connPtr != NULL) {
	    Tcl_DStringStartSublist(&ds);
	    AddElem(&ds, "connid", (unsigned long) connPtr->connid);
	    Tcl_DStringAppendElement(&ds, "peeraddr");
	    Tcl_DStringAppendElement(&ds, ns_inet_ntoa(connPtr->sa.sin_addr));
	    AddElem(&ds, "peerport", (unsigned long) ntohs(connPtr->sa.sin_port));
	    AddElem(&ds, "atime", (unsigned long) connPtr->atime.sec);
	    AddElem(&ds, "mtime", (unsigned long) connPtr->mtime.sec);
	    AddElem(&ds, "nrequest", connPtr->stats.nrequest);
	    AddElem(&ds, "nrecv", connPtr->stats.nrecv);
	    AddElem(&ds, "nsend", connPtr->stats.nsend);
	    Tcl_DStringEndSublist(&ds);
	    connPtr = connPtr->nextPtr;
	}
	if (Tcl_SetVar2(interp, var, "socks", ds.string, TCL_LEAVE_ERR_MSG) == NULL) {
	    break;
	}
	status = TCL_OK;
    } while (0);
    Ns_MutexUnlock(&rpcPtr->lock);
    Tcl_DStringFree(&ds);
    return status;
}


static int
GetRpcClient(ClientData arg, Tcl_Interp *interp, char *client, Rpc **rpcPtrPtr)
{
    Listener *listenerPtr = arg;
    Tcl_HashEntry *hPtr;
 
    hPtr = Tcl_FindHashEntry(&listenerPtr->clients, client);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such client: ", client, NULL);
	return TCL_ERROR;
    }
    *rpcPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * RpcSendCmd --
 *
 *  	Send an RPC request directly, useful for testing.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Depends on command send.
 *
 *----------------------------------------------------------------------
 */

static int
RpcSendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_DString in, out;
    Rpc *rpcPtr;
    int status, cmd;

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " name cmd ?string?\"", NULL);
	return TCL_ERROR;
    }
    if (GetRpcClient(arg, interp, argv[1], &rpcPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &cmd) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_DStringInit(&in);
    Tcl_DStringInit(&out);
    if (argc > 3) {
	Ns_DStringAppend(&in, argv[3]);
    }
    status = Dci_RpcSend((Dci_Rpc *) rpcPtr, cmd, &in, &out);
    Tcl_DStringFree(&in);
    if (status != RPC_OK) {
    	Tcl_DStringFree(&out);
	Tcl_SetResult(interp, Dci_RpcTclError(interp, status), TCL_VOLATILE);
	return TCL_ERROR;
    }
    Tcl_DStringResult(interp, &out);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * RpcClientsCmd --
 *
 *  	Return list of RPC clients.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
RpcClientsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Listener *listenerPtr = arg;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&listenerPtr->clients, &search);
    while (hPtr != NULL) {
	Tcl_AppendElement(interp, Tcl_GetHashKey(&listenerPtr->clients, hPtr));
	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}

static Conn *
RpcNewConn(Rpc *rpcPtr, int sock, int http)
{
    Conn *connPtr;
    socklen_t n;

    connPtr = ns_calloc(1, sizeof(Conn));
    Ns_DStringInit(&connPtr->buf);
    connPtr->msg.msg_iov = connPtr->iov;
    connPtr->msg.msg_iovlen = 2;
    connPtr->http = http;
    connPtr->rpcPtr = rpcPtr;
    connPtr->sock = sock;
    Ns_GetTime(&connPtr->atime);
    n = sizeof(connPtr->sa);
    getpeername(sock, (struct sockaddr *) &connPtr->sa, &n);
    return connPtr;
}

static void
RpcSetSend(Conn *connPtr, Dci_RpcJob *jobPtr)
{
    RpcMsg *msgPtr;

    /*
     * Set the request input.
     */

    connPtr->jobPtr = jobPtr;
    if (jobPtr->inPtr == NULL) {
    	connPtr->iov[1].iov_base = NULL;
    	connPtr->iov[1].iov_len = 0;
    } else {
    	connPtr->iov[1].iov_base = jobPtr->inPtr->string;
    	connPtr->iov[1].iov_len = jobPtr->inPtr->length;
    }

    /*
     * Format the header. 
     */

    Ns_DStringInit(&connPtr->buf);
    if (!connPtr->http) {
	Ns_DStringSetLength(&connPtr->buf, sizeof(RpcMsg));
	msgPtr = (RpcMsg *) connPtr->buf.string;
	msgPtr->cmd = htonl(connPtr->jobPtr->cmd);
	msgPtr->len = htonl(connPtr->iov[1].iov_len);
    } else {
    	Ns_DStringPrintf(&connPtr->buf,
		"%s /%s/%d HTTP/1.0\r\n"
		"host: %s\r\n"
		"content-length: %d\r\n"
		"connection: %s\r\n"
		"\r\n",
		connPtr->rpcPtr->httpMethod,
		connPtr->rpcPtr->name, connPtr->jobPtr->cmd,
		connPtr->rpcPtr->host, connPtr->iov[1].iov_len,
                connPtr->rpcPtr->httpKeepAlive ? "keep-alive" : "close");
    }
    connPtr->iov[0].iov_base = connPtr->buf.string;
    connPtr->iov[0].iov_len = connPtr->buf.length;
    connPtr->len = connPtr->iov[0].iov_len + connPtr->iov[1].iov_len;
}


static void
RpcSetRecv(Conn *connPtr)
{
    connPtr->iov[1].iov_base = NULL;
    connPtr->iov[1].iov_len = 0;
    if (connPtr->http) {
	/* NB: Read up to max header response in buf. */
    	Ns_DStringSetLength(&connPtr->buf, connPtr->rpcPtr->httpmaxhdr);
    } else {
	/* NB: Read RpcMsg size into hdr and up to available output. */
	Ns_DStringSetLength(&connPtr->buf, sizeof(RpcMsg));
	if (connPtr->jobPtr->outPtr != NULL) {
    	    Ns_DStringSetLength(connPtr->jobPtr->outPtr,
			    	connPtr->jobPtr->outPtr->spaceAvl - 1);
    	    connPtr->iov[1].iov_base = connPtr->jobPtr->outPtr->string;
    	    connPtr->iov[1].iov_len = connPtr->jobPtr->outPtr->length;
	}
    }
    connPtr->iov[0].iov_base = connPtr->buf.string;
    connPtr->iov[0].iov_len = connPtr->buf.length;
    connPtr->len = connPtr->iov[0].iov_len + connPtr->iov[1].iov_len;
    connPtr->body = NULL;
}


#define CHDR "connection:"
#define LHDR "content-length:"
#define RHDR "x-dcirpc-result:"
#define CHSZ (sizeof(CHDR)-1)
#define LHSZ (sizeof(LHDR)-1)
#define RHSZ (sizeof(RHDR)-1)

static int
RpcParseHttp(char *hdrs, Parse *parsePtr)
{
    char *p, *q, *l, *r, *c;

    l = r = c = NULL;
    do {
	p = strchr(hdrs, '\n');
	if (p != NULL) {
	    *p++ = '\0';
	}
	q = Ns_StrTrim(hdrs);
	if (strncasecmp(q, RHDR, RHSZ) == 0) {
	    r = Ns_StrTrim(q+RHSZ);
	} else if (strncasecmp(q, LHDR, LHSZ) == 0) {
	    l = Ns_StrTrim(q+LHSZ);
	} else if (strncasecmp(q, CHDR, CHSZ) == 0) {
	    c = Ns_StrTrim(q+CHSZ);
	}
	hdrs = p;
    } while (hdrs != NULL);
    if (r == NULL || Tcl_GetInt(NULL, r, &parsePtr->result) != TCL_OK) {
	return 0;
    }
    if (l == NULL) {
    	parsePtr->length = 0;
    } else if (Tcl_GetInt(NULL, l, &parsePtr->length) != TCL_OK) {
	return 0;
    }
    if (c != NULL && STRIEQ(c, "keep-alive")) {
	parsePtr->keep = 1;
    } else {
	parsePtr->keep = 0;
    }
    return 1;
}
