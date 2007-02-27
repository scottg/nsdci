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

#include "dciInt.h"

static char rcsid[] = "$Id$";

/*
 * The following are bits which define the state of the transaction.  Note
 * that unlike in the old dci code, these bits are not used to synchronize
 * between threads, only to support the legacy http.pending and http.state
 * commands to query the state and the end-of-headers check.
 */

#define REQ_SEND        1
#define REQ_RECV        2
#define REQ_DONE        4
#define REQ_CANCEL      8
#define REQ_EOF         16
#define REQ_ERR         32
#define REQ_TIMEOUT     64 
#define REQ_ANY         (0xff)

/* 
 * The following flags manage transactions internally. 
 */

#define FLAG_DEBUG       1
#define FLAG_EOH         2
#define FLAG_SHUTDOWN	 4
#define FLAG_ASYNC	 8

/*
 * The following callbacks are used to copy content to
 * dstrings or files after the headers are parsed in 
 * HttpProc.
 */

typedef int (CopyProc) (void *arg, char *buf, int len);
static CopyProc CopyDs;
static CopyProc CopyFd;

/*
 * The following struct defines an HTTP transaction either
 * run directly or via the task threads.
 */

typedef struct Http {
    struct Http *nextPtr;
    SOCKET sock;	 /* HTTP socket. */
    Ns_Task *task;	 /* Task struct. */
    unsigned int id;	 /* Unique xact id. */
    int state;		 /* Xact state bits. */
    int flags;		 /* Internal flags (debug, eoh). */
    char *url;		 /* Copy of URL for logging. */
    char *error;	 /* Error string or NULL on ok xact. */
    Ns_Time stime;	 /* Xact start time. */
    Ns_Time etime;	 /* Xact end time. */
    Ns_Time dtime;	 /* Xact elapased time. */
    Ns_Time *timeoutPtr; /* Task timeout used during a direct run. */
    CopyProc *cproc;	 /* Copy callback. */
    void *carg;	 	 /* Copy arg, often content dstring below. */
    size_t ncopy;	 /* Total # bytes copied, response  and content. */
    char *next;		 /* Next byte to send on request. */
    size_t len;		 /* Bytes remaining to send in request. */ 
    Tcl_DString rbuf;	 /* Buffer for request and response before content. */
    Tcl_DString cbuf;    /* Buffer for content if required. */
    Tcl_DString ubuf;	 /* Buffer for copy of URL. */
} Http;

typedef struct Req {
    char *method;
    char *url;
    Ns_Set *hdrs;
    char *body;
    int   flags;
} Req;

typedef struct Stats {
    unsigned int nerr;
    unsigned int nreq;
    unsigned int tmin;
    unsigned int tmax;
    ns_uint64    ttot;
} Stats;

/*
 * Local functions defined in this file
 */

static int HttpGet(Tcl_Interp *interp, Req *reqPtr, int timeout,
		   int *statusPtr, CopyProc *proc, void *arg);
static int HttpGet2(Tcl_Interp *interp, Req *reqPtr, int ms,
		   int *statusPtr, CopyProc *proc, void *arg);
static int HttpQueue(Tcl_HashTable *tablePtr, Tcl_Interp *interp, Req *reqPtr);
static Http *HttpOpen(Tcl_Interp *interp, Req *reqPtr);
static int HttpWait(Http *httpPtr, Ns_Time *timeoutPtr);
static int HttpAbort(Http *httpPtr);
static void HttpClose(Http *httpPtr);
static void HttpCleanup(Tcl_HashTable *tablePtr);
static void HttpParse(char *boh, int *statusPtr, Ns_Set *hdrs);
static void SetBit(char *path, char *key, int bit);
static void SetState(Http *httpPtr, int state);
static int GetState(Http *httpPtr);
static int GetHttp(Tcl_HashTable *tablePtr, Tcl_Interp *interp, char *id,
		  Http **httpPtrPtr, int delete);
static void InitReq(Req *reqPtr);
static void HttpFree(Http *httpPtr);

static void HttpLog(Http *httpPtr, char *what, int syscall, int result,
		    Ns_Time *timePtr);
#define Log(h,w)	HttpLog(h,w,0,-1,NULL)
#define LogTime(h,w,t)	HttpLog(h,w,0,-1,t)
#define LogProc(h,w,n)	HttpLog(h,w,1,n,NULL)

static Tcl_InterpDeleteProc FreeTable;
static Ns_TaskProc HttpProc;
static Tcl_ObjCmdProc HttpObjCmd;
static Tcl_CmdProc HttpGetCmd;
static Tcl_CmdProc HttpQueueCmd;
static Tcl_CmdProc HttpCopyCmd;
static Tcl_CmdProc HttpReturnCmd;
static Tcl_CmdProc HttpWaitCmd;
static Tcl_CmdProc HttpCancelCmd;
static Tcl_CmdProc HttpStatusCmd;
static Tcl_CmdProc HttpPendingCmd;
static int GetCmds(ClientData arg, Tcl_Interp *interp, int argc, char **argv,
		   int queue);
static Ns_TaskQueue *GetQueue(Http *httpPtr);

static Http *firstHttpPtr;
static unsigned int nextid;
static Ns_Mutex statelock;
static Ns_Mutex idlock;
static Ns_Mutex alloclock;
static char *agent;
static int logFd = -1;
static Ns_Cache *stats;
static int fnscmds;
static int fhttpcmds;
static int flags;
static int bufsize;
static Ns_Tls bufid;


void
DciHttpLibInit(void)
{
    DciAddIdent(rcsid);
    Ns_MutexSetName(&idlock, "nshttp:id");
    Ns_MutexSetName(&statelock, "nshttp:state");
    Ns_MutexSetName(&alloclock, "nshttp:alloc");
    Ns_TlsAlloc(&bufid, ns_free);
    stats = Ns_CacheCreateSz("nshttp:stats", TCL_STRING_KEYS, 1000, ns_free);
    fnscmds = 0;
    fhttpcmds = 1;
    flags = (FLAG_SHUTDOWN | FLAG_ASYNC);
    agent = "NSHTTP/" DCI_VERSION;
    bufsize = 4000;
}


int
DciHttpModInit(char *server, char *module)
{
    char *path, *logfile;
    int opt;

    path = Ns_ConfigGetPath(server, module, "http", NULL);
    logfile = Ns_ConfigGet(path, "logfile");
    if (logfile != NULL &&
    	    (logFd = open(logfile, O_WRONLY|O_APPEND|O_CREAT, 0644)) < 0) {
	Ns_Log(Error, "%s: could not open log %s: %s",
			module, logfile, strerror(errno));
	return NS_ERROR;
    }

    /*
     * Config options for which of the ns_http or http.* commands
     * to enable.
     */

    if (Ns_ConfigGetBool(path, "httpcmds", &opt)) {
	fhttpcmds = opt;
    }
    if (Ns_ConfigGetBool(path, "nscmds", &opt)) {
	fnscmds = opt;
    }

    /*
     * The Ns_Task read callback uses a large per-thread buffer.
     */

    if (Ns_ConfigGetInt(path, "bufsize", &opt) && opt > 0) {
	bufsize = opt;
    }

    /*
     * Setting async to 0 turns all queue/waits into direct run.
     */

    SetBit(path, "async", FLAG_ASYNC);


    /*
     * Setting shutdown to 1 will call shutdown() after requests are sent
     * which makes it clear we're done sending.  Some SAPI servers have 
     * had problems with this in the past.
     */

    SetBit(path, "shutdown", FLAG_SHUTDOWN);

    return NS_OK;
}


static void
SetBit(char *path, char *key, int bit)
{
    int bool;

    if (Ns_ConfigGetBool(path, key, &bool)) {
	if (bool) {
	    flags |= bit;
	} else {
	    flags &= ~bit;
	}
    }
}


int
DciHttpTclInit(Tcl_Interp *interp)
{
    Tcl_HashTable *tablePtr;

    tablePtr = ns_malloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
    Tcl_SetAssocData(interp, "nshttp", FreeTable, tablePtr);

    if (fnscmds) {
    	Tcl_CreateObjCommand(interp, "ns_http", HttpObjCmd, tablePtr, NULL);
    }
    if (fhttpcmds) {
    	Tcl_CreateCommand(interp, "http.get", HttpGetCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.queue", HttpQueueCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.copy", HttpCopyCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.return", HttpReturnCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.wait", HttpWaitCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.cancel", HttpCancelCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.status", HttpStatusCmd, tablePtr, NULL);
    	Tcl_CreateCommand(interp, "http.pending", HttpPendingCmd, tablePtr, NULL);
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpObjCmd --
 *
 *	Implements ns_http to handle HTTP requests.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May queue an HTTP request.
 *
 *----------------------------------------------------------------------
 */

static int
HttpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_HashTable *tablePtr = arg;
    Stats *statsPtr;
    Tcl_DString ds;
    Req req;
    Http *httpPtr;
    char buf[100], *url, *result, *carg, *err, *pattern;
    int status, timeidx, bool, bit;
    Ns_Time timeout, incr;
    Ns_Set *hdrs;
    Ns_Entry *entry;
    Ns_CacheSearch search;
    static CONST char *opts[] = {
       "async", "debug", "cancel", "cleanup", "queue", "wait",
       "stats", "shutdown", NULL
    };
    enum {
	HAsyncIdx, HDebugIdx, HCancelIdx, HCleanupIdx, HQueueIdx,
	HWaitIdx, HStatsIdx, HShutdownIdx
    } opt;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case HAsyncIdx:
    case HDebugIdx:
    case HShutdownIdx:
	if (opt == HAsyncIdx) {
	    bit = FLAG_ASYNC;
	} else if (opt == HShutdownIdx) {
	    bit = FLAG_SHUTDOWN;
	} else {
	    bit = FLAG_DEBUG;
	}
	if (objc > 2) {
	    if (Tcl_GetBooleanFromObj(interp, objv[2], &bool) != TCL_OK) {
	    	return TCL_ERROR;
	    }
	    if (bool) {
		flags |= bit;
	    } else {
		flags &= ~bit;
	    }
	}
        Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (flags & bit));
	break;

    case HQueueIdx:
        if (objc < 4 || objc > 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "method url ?body? ?headers?");
            return TCL_ERROR;
        }
	InitReq(&req);
        req.method = Tcl_GetString(objv[2]);
        req.url    = Tcl_GetString(objv[3]);
	if (objc > 4) {
	    req.body = Tcl_GetString(objv[4]);
	}
        if (objc > 5) {
	    req.hdrs = Ns_TclGetSet(interp, Tcl_GetString(objv[5]));
	    if (req.hdrs == NULL) {
                return TCL_ERROR;
            }
        }
	if (HttpQueue(tablePtr, interp, &req) != TCL_OK) {
	    return TCL_ERROR;
	}
        break;

    case HWaitIdx:
        if (objc < 4 || objc > 8) {
            Tcl_WrongNumArgs(interp, 2, objv,
		"id resultsVar ?timeout? ?headers? ?-servicetime svcTime?");
            return TCL_ERROR;
        }
        carg = Tcl_GetString(objv[objc - 2]);
        if (STRIEQ(carg, "-servicetime")) {
            timeidx = objc - 1;
            objc -= 2; /* so I don't have to refactor the rest of the code */
        } else {
            timeidx = 0;
        }
        if (objc < 5) {
            incr.sec  = 2;
            incr.usec = 0;
        } else if (Ns_TclGetTimeFromObj(interp, objv[4], &incr) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_GetTime(&timeout);
        Ns_IncrTime(&timeout, incr.sec, incr.usec);
        if (objc < 6) {
            hdrs = NULL;
        } else {
	    hdrs = Ns_TclGetSet(interp, Tcl_GetString(objv[5]));
	    if (hdrs == NULL) {
            	return TCL_ERROR;
	    }
        }
	if (GetHttp(tablePtr, interp, Tcl_GetString(objv[2]), &httpPtr, 1)
		!= TCL_OK) {
	    return TCL_ERROR;
	}
	status = HttpWait(httpPtr, &timeout);
        if (status != NS_OK) {
	    Tcl_SetResult(interp, "timeout", TCL_STATIC);
        } else {
            if (httpPtr->error != NULL) {
                status = NS_ERROR;
                result = httpPtr->error;
            } else {
		if (hdrs != NULL) {
                    HttpParse(httpPtr->rbuf.string, NULL, hdrs);
		}
		if (timeidx > 0) {
            	    snprintf(buf, 50, "%ld:%ld", httpPtr->dtime.sec, httpPtr->dtime.usec);
            	    (void) Tcl_SetVar(interp, Tcl_GetString(objv[timeidx]), buf, 0);
		}
		result = httpPtr->cbuf.string;
            }
            err = Tcl_SetVar(interp, Tcl_GetString(objv[3]), result,
				TCL_LEAVE_ERR_MSG);
            HttpClose(httpPtr);
	    if (err == NULL) {
            	return TCL_ERROR;
	    }
        }
        Tcl_SetBooleanObj(Tcl_GetObjResult(interp), status == NS_OK ? 1 : 0);
        break;

    case HCancelIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "id");
            return TCL_ERROR;
        }
	if (GetHttp(tablePtr, interp, Tcl_GetString(objv[2]), &httpPtr, 1)
		!= TCL_OK) {
	    return TCL_ERROR;
	}
	Log(httpPtr, "cancel");
    	Tcl_SetIntObj(Tcl_GetObjResult(interp), HttpAbort(httpPtr));
        break;

    case HCleanupIdx:
	HttpCleanup(tablePtr);
        break;

    case HStatsIdx:
        pattern = ((objc > 2) ? Tcl_GetString(objv[2]) : NULL);
	Tcl_DStringInit(&ds);
	Ns_CacheLock(stats);
	entry = Ns_CacheFirstEntry(stats, &search);
	while (entry != NULL) {
	    url = Ns_CacheKey(entry);
	    if (pattern == NULL || Tcl_StringMatch(url, pattern)) {
	    	statsPtr = Ns_CacheGetValue(entry);
	    	Tcl_DStringStartSublist(&ds);
	    	Tcl_DStringAppendElement(&ds, url);
	    	sprintf(buf, " err %u nreq %u min %u max %u avg %u",
			statsPtr->nerr, statsPtr->nreq,
			statsPtr->tmin, statsPtr->tmax,
			(unsigned int) (statsPtr->ttot / statsPtr->nreq));
	    	Tcl_DStringAppend(&ds, buf, -1);
	    	Tcl_DStringEndSublist(&ds);
	    }
            entry = Ns_CacheNextEntry(&search);
	}
	Ns_CacheUnlock(stats);
	Tcl_DStringResult(interp, &ds);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpGetCmd --
 *
 *	Implements http.get and http.queue.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May queue or fetch an HTTP request.
 *
 *----------------------------------------------------------------------
 */

static int
HttpGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return GetCmds(arg, interp, argc, argv, 0);
}

static int
HttpQueueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return GetCmds(arg, interp, argc, argv, 1);
}

static int
GetCmds(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int queue)
{
    Tcl_HashTable *tablePtr = arg;
    Tcl_DString ds;
    Req req;
    int i, timeout, result, status;

    if (argc < 2 || argc > 12) {
badargs:
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
		" ?-debug? ?-timeout #? ?-method method?"
		" ?-headers set? ?-body body? url\"", NULL);
	return TCL_ERROR;
    }
    InitReq(&req);
    timeout = 2;
    for (i = 1; i < argc; ++i) {
	if (STREQ(argv[i], "-debug")) {
            req.flags |= FLAG_DEBUG;
	} else if (STREQ(argv[i], "-headers")) {
	    ++i;
	    req.hdrs = Ns_TclGetSet(interp, argv[i]);
	    if (req.hdrs == NULL) {
		return TCL_ERROR;
	    }
	} else if (STREQ(argv[i], "-timeout")) {
	    ++i;
	    if (argv[i] == NULL) goto badargs;
	    if (Tcl_GetInt(interp, argv[i], &timeout) != TCL_OK) {
		return TCL_ERROR;
	    }
	} else if (STREQ(argv[i], "-method")) {
            ++i;
	    if (argv[i] == NULL) goto badargs;
            req.method = argv[i];
	} else if (STREQ(argv[i], "-body")) {
            ++i;
	    if (argv[i] == NULL) goto badargs;
            req.body = argv[i];
        } else {
	    req.url = argv[i];
	}
    }
    if (req.url == NULL) goto badargs;
    result = TCL_OK;
    if (queue) {
	result = HttpQueue(tablePtr, interp, &req);
    } else {
    	Tcl_DStringInit(&ds);
	if (HttpGet(interp, &req, timeout, &status, CopyDs, &ds) < 0) {
	    result = TCL_ERROR;
	} else {
	    Tcl_DStringResult(interp, &ds);
        }
    	Tcl_DStringFree(&ds);
    }    
    return result;
}


static int
HttpCopyCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Channel chan;
    int fd, status, mode;
    int i, ncopy, timeout;
    char *fileId;
    Req req;

    if (argc < 3) {
badargs:
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?-headers set? ?-timeout seconds? url fileId\"", NULL);
	return TCL_ERROR;
    }
    InitReq(&req);
    fileId = NULL;
    timeout = 1;
    for (i = 1; i < argc; ++i) {
	if (STREQ(argv[i], "-headers")) {
	    ++i;
	    req.hdrs = Ns_TclGetSet(interp, argv[i]);
	    if (req.hdrs == NULL) {
		return TCL_ERROR;
	    }
	} else if (STREQ(argv[i], "-timeout")) {
	    ++i;
	    if (argv[i] == NULL) goto badargs;
	    if (Tcl_GetInt(interp, argv[i], &timeout) != TCL_OK) {
		return TCL_ERROR;
	    }
	} else {
	    req.url = argv[i++];
            if (i == argc) {
		goto badargs;
	    }
            fileId = argv[i++];
            if (i < argc 
		    && Tcl_GetInt(interp, argv[i], &timeout) != TCL_OK) {
                return TCL_ERROR;
            }
	}
    }
    if (req.url == NULL) {
	goto badargs;
    }
    chan = Tcl_GetChannel(interp, fileId, &mode);
    if (chan == NULL) {
        return TCL_ERROR;
    }
    if (!(mode & TCL_WRITABLE)) {
        Tcl_AppendResult(interp, "channel \"", fileId,
            "\" not open for write", NULL);
        return TCL_ERROR;
    }
    if (Tcl_Flush(chan) != TCL_OK) {
        Tcl_AppendResult(interp, "error flushing \"", fileId, "\": ",
		Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }

    /*
     * TODO: Check if this should just be Tcl_Write on the channel?
     */

    if (Ns_TclGetOpenFd(interp, fileId, 1, &fd) != TCL_OK) {
	return TCL_ERROR;
    }
    ncopy = HttpGet(interp, &req, timeout, &status, CopyFd, (void *) fd);
    if (ncopy < 0) {
	return TCL_ERROR;
    }
    Tcl_SetIntObj(Tcl_GetObjResult(interp), status);
    return TCL_OK;
}


static int
HttpReturnCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int timeout, status, result;
    Tcl_DString ds;
    Ns_Conn *conn;
    Ns_Set *hdrs;
    Req req;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " url ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    if (argc == 2) {
	timeout = 1;
    } else if (Tcl_GetInt(interp, argv[2], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    result = TCL_ERROR;
    Ns_DStringInit(&ds);
    InitReq(&req);
    req.url = argv[1];
    req.hdrs = hdrs = Ns_SetCreate(NULL);
    if (HttpGet(interp, &req, timeout, &status, CopyDs, &ds) < 0) {
	goto done;
    }
    Ns_SetPut(hdrs, "Via", agent);
    Ns_ConnReplaceHeaders(conn, hdrs);
    status = Ns_ConnFlushHeaders(conn, status);
    if (status == NS_OK) {
        if (!(conn->flags & NS_CONN_SKIPBODY)) {
            status = Ns_WriteConn(conn, ds.string, ds.length);
        }
        if (status == NS_OK) {
            status = Ns_ConnClose(conn);
        }
    }
    if (status == NS_OK) {
	result = TCL_OK;
    } else {
	Tcl_SetResult(interp, "send data failed", TCL_STATIC);
    }

done:
    Ns_SetFree(hdrs);
    Tcl_DStringFree(&ds);
    return result;
}


static int
HttpPendingCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashTable *tablePtr = arg;
    Http *httpPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    int state;
    char *p;

    if (argc > 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?state?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
	state = REQ_ANY;
    } else {
	state = 0;
	p = argv[1];
	while (*p != '\0') {
	    switch (*p) {
		case 's':
			state |= REQ_SEND;
			break;
		case 'r':
			state |= REQ_RECV;
			break;
		case 'c':
			state |= REQ_CANCEL;
			break;
		case 't':
			state |= REQ_TIMEOUT;
			break;
		case ' ':
		case '\t':
		case '\0':
			break;
		default:
			Tcl_AppendResult(interp, "invalid state \"",
				argv[1], "\": should be one or more of "
				"s, r, c, or t", NULL);
			return TCL_ERROR;
		}
	    ++p;
	}
    }
    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
	httpPtr = Tcl_GetHashValue(hPtr);
	if (state & GetState(httpPtr)) {
	    Tcl_AppendElement(interp, Tcl_GetHashKey(tablePtr, hPtr));
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


static int
HttpWaitCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashTable *tablePtr = arg;
    Http *httpPtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " id\"", NULL);
	return TCL_ERROR;
    }
    if (GetHttp(tablePtr, interp, argv[1], &httpPtr, 1) != TCL_OK) {
	return TCL_ERROR;
    }
    (void) HttpWait(httpPtr, NULL);
    if (httpPtr->error == NULL) {
	Tcl_SetResult(interp, httpPtr->cbuf.string, TCL_VOLATILE);
    }
    HttpClose(httpPtr);
    return TCL_OK;
}


static int
HttpCancelCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashTable *tablePtr = arg;
    Http *httpPtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " id\"", NULL);
	return TCL_ERROR;
    }
    if (GetHttp(tablePtr, interp, argv[1], &httpPtr, 0) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Ns_TaskCancel(httpPtr->task) != NS_OK) {
	Tcl_AppendResult(interp, "could not cancel: ", argv[1], NULL);
	return TCL_ERROR;
    }
    Log(httpPtr, "cancel");
    Tcl_SetIntObj(Tcl_GetObjResult(interp), GetState(httpPtr));
    return TCL_OK;
}


static int
HttpStatusCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashTable *tablePtr = arg;
    Http *httpPtr;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " id\"", NULL);
	return TCL_ERROR;
    }
    if (GetHttp(tablePtr, interp, argv[1], &httpPtr, 0) != TCL_OK) {
	return TCL_ERROR;
    }
    Log(httpPtr, "status");
    Tcl_SetIntObj(Tcl_GetObjResult(interp), GetState(httpPtr));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpOpen --
 *
 *        Open a connection to the given URL host and construct
 *        an Http structure to fetch the file.
 *
 * Results:
 *        Pointer to Http struct or NULL on error.
 *
 * Side effects:
 *        Will open a socket connection.
 *
 *----------------------------------------------------------------------
 */

Http *
HttpOpen(Tcl_Interp *interp, Req *reqPtr)
{
    Http *httpPtr;
    char *host, *file, *port;
    int i, len;

    /*
     * Perform simple validation of the URL.
     */

    if (strncmp(reqPtr->url, "http://", 7) != 0 || reqPtr->url[7] == '\0') {
	Tcl_SetErrorCode(interp, "NSHTTP", "INVALID", "invalid url", NULL);
	Tcl_AppendResult(interp, "could not open: ", reqPtr->url, NULL);
        return NULL;
    }

    /*
     * Create the new Http struct and copy the URL to be parsed into
     * host, port, and file (uri).
     */

    Ns_MutexLock(&alloclock);
    httpPtr = firstHttpPtr;
    if (httpPtr != NULL) {
	firstHttpPtr = httpPtr->nextPtr;
    }
    Ns_MutexUnlock(&alloclock);
    if (httpPtr == NULL) {
	httpPtr = ns_malloc(sizeof(Http));
    	Tcl_DStringInit(&httpPtr->rbuf);
    	Tcl_DStringInit(&httpPtr->cbuf);
    	Tcl_DStringInit(&httpPtr->ubuf);
    }
    httpPtr->timeoutPtr = NULL;
    httpPtr->error = NULL;
    httpPtr->state = REQ_SEND;
    httpPtr->flags = reqPtr->flags;
    httpPtr->ncopy = 0;
    httpPtr->cproc = CopyDs;
    httpPtr->carg = &httpPtr->cbuf;
    Tcl_DStringAppend(&httpPtr->ubuf, reqPtr->url, -1);
    httpPtr->url = httpPtr->ubuf.string;

    host = httpPtr->url + 7;
    file = strchr(host, '/');
    if (file != NULL) {
        *file = '\0';
    }
    port = strchr(host, ':');
    if (port == NULL) {
        i = 80;
    } else {
        *port = '\0';
        i = strtoul(port+1, NULL, 0);
    }
    httpPtr->sock = Ns_SockAsyncConnect(host, i);
    if (httpPtr->sock == INVALID_SOCKET) {
	Tcl_AppendResult(interp, "could not connect to \"", host,
		"\": ", Tcl_PosixError(interp), NULL);
	HttpFree(httpPtr);
	return NULL;
    }

    /*
     * Setup the Http struct to send the request.
     */

    Ns_SockSetNonBlocking(httpPtr->sock);
    Ns_MutexLock(&idlock);
    httpPtr->id = nextid++;
    Ns_MutexUnlock(&idlock);
    Ns_GetTime(&httpPtr->stime);
    httpPtr->task = Ns_TaskCreate(httpPtr->sock, HttpProc, httpPtr);
    if (reqPtr->method == NULL) {
        Ns_DStringAppend(&httpPtr->rbuf, "GET");
    } else {
        Ns_DStringAppend(&httpPtr->rbuf, reqPtr->method);
        Ns_StrToUpper(httpPtr->rbuf.string);
    }
    Ns_DStringVarAppend(&httpPtr->rbuf,
			" /", file ? file + 1 : "", " HTTP/1.1\r\n", NULL);
    if (port != NULL) {
        *port = ':';
    }
    Ns_DStringVarAppend(&httpPtr->rbuf,
        "User-Agent: ", agent, "\r\n"
        "Connection: close\r\n"
        "Host: ", host, "\r\n", NULL);
    if (file != NULL) {
        *file = '/';
    }
    for (i = 0; reqPtr->hdrs != NULL && i < Ns_SetSize(reqPtr->hdrs); i++) {
        Ns_DStringVarAppend(&httpPtr->rbuf,
            Ns_SetKey(reqPtr->hdrs, i), ": ",
	    Ns_SetValue(reqPtr->hdrs, i), "\r\n", NULL);
    }
    len = reqPtr->body ? strlen(reqPtr->body) : 0;
    if (len > 0) {
        Ns_DStringPrintf(&httpPtr->rbuf, "Content-Length: %d\r\n", len);
    }
    Tcl_DStringAppend(&httpPtr->rbuf, "\r\n", 2);
    if (len > 0) {
        Tcl_DStringAppend(&httpPtr->rbuf, reqPtr->body, len);
    }
    httpPtr->next = httpPtr->rbuf.string;
    httpPtr->len = httpPtr->rbuf.length;
    Log(httpPtr, "open");
    return httpPtr;
}


static void
HttpClose(Http *httpPtr)
{
    Stats *statsPtr;
    Ns_Entry *entry;
    int new, ms;

    Log(httpPtr, "close");
    ms = httpPtr->dtime.sec * 1000 + httpPtr->dtime.usec / 1000;
    Ns_CacheLock(stats);
    entry = Ns_CacheCreateEntry(stats, httpPtr->url, &new);
    if (!new) {
	statsPtr = Ns_CacheGetValue(entry);
    } else {
	statsPtr = ns_calloc(1, sizeof(Stats));
	Ns_CacheSetValue(entry, statsPtr);
    }
    if (httpPtr->error) {
	++statsPtr->nerr;
    } else {
    	++statsPtr->nreq;
	if (new) {
	    statsPtr->tmin = statsPtr->tmax = ms;
	    statsPtr->ttot = ms;
	} else {
    	    if (statsPtr->tmin > ms) {
	    	statsPtr->tmin = ms;
    	    }
    	    if (statsPtr->tmax < ms) {
	    	statsPtr->tmax = ms;
    	    }
	    statsPtr->ttot += ms;
	}
    }
    Ns_CacheUnlock(stats);
    Ns_TaskFree(httpPtr->task);
    ns_sockclose(httpPtr->sock);
    HttpFree(httpPtr);
}


static void
HttpFree(Http *httpPtr)
{
    Tcl_DStringTrunc(&httpPtr->rbuf, 0);
    Tcl_DStringTrunc(&httpPtr->cbuf, 0);
    Tcl_DStringTrunc(&httpPtr->ubuf, 0);
    Ns_MutexLock(&alloclock);
    httpPtr->nextPtr = firstHttpPtr;
    firstHttpPtr = httpPtr;
    Ns_MutexUnlock(&alloclock);
}


static int
HttpAbort(Http *httpPtr)
{
    Log(httpPtr, "abort");
    if (httpPtr->flags & FLAG_ASYNC) {
    	Ns_TaskCancel(httpPtr->task);
    	Ns_TaskWait(httpPtr->task, NULL);
    }
    HttpClose(httpPtr);
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpProc --
 *
 *        Task callback for ns_http connections.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Will call Ns_TaskCallback and Ns_TaskDone to manage state
 *        of task.
 *
 *----------------------------------------------------------------------
 */

static void
HttpProc(Ns_Task *task, SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;
    char *buf, *eoh, *start;
    int n, state, skip, trunc;

    state = REQ_DONE;
    switch (why) {
    case NS_SOCK_INIT:
    	Log(httpPtr, "init");
	Ns_TaskCallback(task, NS_SOCK_WRITE, httpPtr->timeoutPtr);
	return;
	break;

    case NS_SOCK_WRITE:
    	n = send(sock, httpPtr->next, httpPtr->len, 0);
    	LogProc(httpPtr, "send", n);
    	if (n < 0) {
	    httpPtr->error = "send failed";
	    state |= REQ_ERR;
	} else {
    	    httpPtr->next += n;
    	    httpPtr->len -= n;
    	    if (httpPtr->len == 0) {
            	if (httpPtr->flags & FLAG_SHUTDOWN) {
		    shutdown(sock, 1);
		}
            	Tcl_DStringTrunc(&httpPtr->rbuf, 0);
	    	Ns_TaskCallback(task, NS_SOCK_READ, httpPtr->timeoutPtr);
	    	SetState(httpPtr, REQ_RECV);
	    }
	    return;
	}
	break;

    case NS_SOCK_READ:
	buf = Ns_TlsGet(&bufid);
	if (buf == NULL) {
	    buf = ns_malloc(bufsize);
	    Ns_TlsSet(&bufid, buf);
	}
    	n = recv(sock, buf, bufsize, 0);
    	LogProc(httpPtr, "recv", n);
	if (n < 0) {
	    state |= REQ_ERR;
	    httpPtr->error = "recv failed";
	} else if (n == 0) {
	    state |= REQ_EOF;
	} else {
	    start = buf;
	    httpPtr->ncopy += n;
	    trunc = 0;
	    if (!(httpPtr->flags & FLAG_EOH)) {
	    	Ns_DStringNAppend(&httpPtr->rbuf, buf, n);
		skip = 2;
	    	eoh = strstr(httpPtr->rbuf.string, "\r\n\r\n");
	    	if (eoh == NULL) {
		    skip = 1;
		    eoh = strstr(httpPtr->rbuf.string, "\n\n");
	    	}
	    	if (eoh != NULL) {
		    httpPtr->flags |= FLAG_EOH;
		    eoh[skip] = '\0';
		    start = eoh + (skip * 2);
		    n = httpPtr->rbuf.length - (start - httpPtr->rbuf.string);
		    trunc = httpPtr->rbuf.length - n;
		}
	    }
	    if ((httpPtr->flags & FLAG_EOH) && n > 0) {
		if (!(httpPtr->cproc)(httpPtr->carg, start, n)) {
		    httpPtr->error = "copy error";
		    state |= REQ_ERR;
		    n = -1;
		}
	    	if (trunc > 0) {
		    Tcl_DStringTrunc(&httpPtr->rbuf, trunc);
	    	}
	    }
	    if (n >= 0) {
	    	return;
	    }
	}
	break;

    case NS_SOCK_TIMEOUT:
    	Log(httpPtr, "timeout");
	httpPtr->error = "timeout";
	state |= REQ_TIMEOUT;
	break;

    case NS_SOCK_EXIT:
    	Log(httpPtr, "shutdown");
	httpPtr->error = "shutdown";
	state |= REQ_CANCEL;
	break;

    case NS_SOCK_CANCEL:
    	Log(httpPtr, "cancelled");
	httpPtr->error = "cancelled";
	state |= REQ_CANCEL;
	break;
    }

    /*
     * Get completion time and mark task as done.
     */
     
    Ns_GetTime(&httpPtr->etime);
    Ns_DiffTime(&httpPtr->etime, &httpPtr->stime, &httpPtr->dtime);
    Ns_TaskDone(httpPtr->task);
    SetState(httpPtr, state);
}


static void
LogWrite(int fd, Ns_DString *bufPtr)
{
    size_t len = (size_t) bufPtr->length;

    if (write(fd, bufPtr->string, len) != len) {
	Ns_Log(Warning, "http: log write failed: %s", strerror(errno));
    }
}


static void
HttpLog(Http *httpPtr, char *what, int syscall, int result, Ns_Time *timePtr)
{
    Ns_DString buf;
    Ns_Time now;
    Ns_Conn *conn;

    if (logFd >= 0 || (httpPtr->flags & FLAG_DEBUG)) {
	conn = Ns_GetConn();
	Ns_GetTime(&now);
	Ns_DStringInit(&buf);
	Ns_DStringPrintf(&buf, "%d %s %d.%d %s %d %s",
		httpPtr->id, Ns_ThreadGetName(), now.sec, now.usec,
		what, httpPtr->sock, httpPtr->url);
	if (syscall) {
	    Ns_DStringPrintf(&buf, " %d %s", result,
				(result < 0 ? strerror(errno) : "ok"));
	}
	if (conn != NULL && conn->request != NULL) {
	    Ns_DStringVarAppend(&buf, " ", conn->request->url, NULL);
	}
	if (timePtr != NULL) {
	    Ns_DStringPrintf(&buf, " %d.%d", timePtr->sec, timePtr->usec);
	}
	Ns_DStringNAppend(&buf, "\n", 1);
	if (logFd >= 0) {
	    LogWrite(logFd, &buf);
	}
	if ((httpPtr->flags & FLAG_DEBUG)) {
	    LogWrite(2, &buf);
	}
	Ns_DStringFree(&buf);
    }
}


static int
GetHttp(Tcl_HashTable *tablePtr, Tcl_Interp *interp, char *id,
       Http **httpPtrPtr, int delete)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(tablePtr, id);
    if (hPtr != NULL) {
	*httpPtrPtr = Tcl_GetHashValue(hPtr);
	if (delete) {
	    Tcl_DeleteHashEntry(hPtr);
	}
	return TCL_OK;
    }
    Tcl_AppendResult(interp, "no such request: ", id, NULL);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpParse --
 *
 *        Parse HTTP response for status code and headers.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Will update statusPtr (if given) and add parsed headers
 *	  to hdrs (if given).
 *
 *----------------------------------------------------------------------
 */

static void
HttpParse(char *boh, int *statusPtr, Ns_Set *hdrs)
{
    int firsthdr, len, major, minor;
    char *eoh, *p;

    /*
     * Parse the headers saved in the dstring if requested.
     */

    if (statusPtr != NULL) {
    	if (sscanf(boh, "HTTP/%d.%d %d", &major, &minor, statusPtr) != 3) {
            statusPtr = 0;
	}
    }
    if (hdrs != NULL) {
	firsthdr = 1;
	p = boh;
	while ((eoh = strchr(p, '\n')) != NULL) {
	    *eoh++ = '\0';
	    len = strlen(p);
	    if (len > 0 && p[len-1] == '\r') {
		p[len-1] = '\0';
	    }
	    if (firsthdr) {
		if (hdrs->name != NULL) {
		    ns_free(hdrs->name);
		}
		hdrs->name = ns_strdup(p);
		firsthdr = 0;
	    } else if (Ns_ParseHeader(hdrs, p, 0) != NS_OK) {
		break;
	    }
	    p = eoh;
	}
    }
}


/*
 * DciHttpGet --
 *
 *	Simple HTTP get used by art and ads API's.
 */

int
DciHttpGet(Tcl_Interp *interp, char *method, char *url, Ns_Set *hdrs, int ms)
{
    Req req;

    InitReq(&req);
    req.method = method;
    req.url = url;
    req.hdrs = hdrs;
    if (HttpGet2(interp, &req, ms, NULL, NULL, NULL) < 0) {
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
HttpGet(Tcl_Interp *interp, Req *reqPtr, int timeout, int *statusPtr,
	CopyProc *proc, void *arg)
{
    return HttpGet2(interp, reqPtr, timeout * 1000, statusPtr, proc, arg);
}


static int
HttpGet2(Tcl_Interp *interp, Req *reqPtr, int ms, int *statusPtr,
	CopyProc *proc, void *arg)
{
    Http *httpPtr;
    int ncopy;
    char buf[32];
    Ns_Time timeout;

    if (ms < 0) {
	sprintf(buf, "%d", ms);
	Tcl_AppendResult(interp, "invalid millisecond timeout: ", buf, NULL);
	return -1;
    }
    httpPtr = HttpOpen(interp, reqPtr);
    if (httpPtr == NULL) {
	return -1;
    }
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, 0, ms * 1000);
    httpPtr->timeoutPtr = &timeout;
    httpPtr->cproc = proc;
    httpPtr->carg = arg;
    if (reqPtr->hdrs != NULL) {
        Ns_SetTrunc(reqPtr->hdrs, 0);
    }
    Log(httpPtr, "get");
    Ns_TaskRun(httpPtr->task);
    if (httpPtr->error != NULL) {
	return -1;
    }
    ncopy = httpPtr->ncopy;
    HttpParse(httpPtr->rbuf.string, statusPtr, reqPtr->hdrs);
    HttpClose(httpPtr);
    return ncopy;
}


static int
CopyDs(void *arg, char *buf, int len)
{
    Tcl_DString *dsPtr = arg;
    
    Tcl_DStringAppend(dsPtr, buf, len);
    return 1;
}


static int
CopyFd(void *arg, char *buf, int len)
{
    int fd = (int) arg;
    
    if (write(fd, buf, (size_t)len) != len) {
    	return 0;
    }
    return 1;
}


static void
FreeTable(ClientData arg, Tcl_Interp *interp)
{
    Tcl_HashTable *tablePtr = arg;

    HttpCleanup(tablePtr);
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}


static void
SetState(Http *httpPtr, int state)
{
    Ns_MutexLock(&statelock);
    httpPtr->state = state;
    Ns_MutexUnlock(&statelock);
}


static int
GetState(Http *httpPtr)
{
    int state;

    Ns_MutexLock(&statelock);
    state = httpPtr->state;
    Ns_MutexUnlock(&statelock);
    return state;
}


static int
HttpQueue(Tcl_HashTable *tablePtr, Tcl_Interp *interp, Req *reqPtr)
{
    Http *httpPtr;
    int n, new;
    char buf[100];
    Tcl_HashEntry *hPtr;

    httpPtr = HttpOpen(interp, reqPtr);
    if (httpPtr == NULL) {
        return TCL_ERROR;
    }
    Log(httpPtr, "queue");
    if ((httpPtr->flags & FLAG_ASYNC)
	    && Ns_TaskEnqueue(httpPtr->task, GetQueue(httpPtr)) != NS_OK) {
	Tcl_AppendResult(interp, "could not queue: ", httpPtr->url, NULL);
	HttpClose(httpPtr);
	return TCL_ERROR;
    }
    n = tablePtr->numEntries;
    do {
        sprintf(buf, "http%d", n++);
        hPtr = Tcl_CreateHashEntry(tablePtr, buf, &new);
    } while (!new);
    Tcl_SetHashValue(hPtr, httpPtr);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
HttpWait(Http *httpPtr, Ns_Time *timeoutPtr)
{
    LogTime(httpPtr, "wait", timeoutPtr);
    if (!(httpPtr->flags & FLAG_ASYNC)) {
	httpPtr->timeoutPtr = timeoutPtr;
	Ns_TaskRun(httpPtr->task);
    }
    return Ns_TaskWait(httpPtr->task, timeoutPtr);
}


static void
HttpCleanup(Tcl_HashTable *tablePtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Http *httpPtr;
 
    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
        httpPtr = Tcl_GetHashValue(hPtr);
	Log(httpPtr, "cleanup");
        (void) HttpAbort(httpPtr);
	Tcl_DeleteHashEntry(hPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
}


static Ns_TaskQueue *
GetQueue(Http *httpPtr)
{
    static volatile Ns_TaskQueue *queue;

    if (queue == NULL) {
	Ns_MasterLock();
	if (queue == NULL) {
    	    queue = Ns_CreateTaskQueue("nshttp");
	}
	Ns_MasterUnlock();
    }
    return (Ns_TaskQueue *) queue;
}


static void
InitReq(Req *reqPtr)
{
    memset(reqPtr, 0, sizeof(Req));
    reqPtr->flags = flags;
}
