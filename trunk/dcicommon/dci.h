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

#ifndef DCIC_H
#define DCIC_H

#include "ns.h"
#define DCI_MAJOR 5
#define DCI_MINOR 0
#define DCI_VERSION "5.0"

#ifdef _DCIBUILD
#include "dciInt.h"
#endif

/*
 * Result codes for RPC requests:
 *
 * RPC_OK		Request sent and response received.
 * RPC_ENOCON		No connection available for request.
 * RPC_EHTTPSIZE	HTTP response oversized.
 * RPC_EHTTPPARSE	HTTP response not a valid RPC response.
 * RPC_ESEND		Failure during socket send.
 * RPC_ERECV		Failure during socket recv.
 * RPC_ESENDWAIT	Timeout waiting for initial or continuing send.
 * RPC_ERECVWAIT	Timeout waiting for followup recv.
 * RPC_ETIMEOUT		Timeout waiting for start of server response.
 *
 * Note that the RPC job result code will only indicate a valid response
 * from the remote if the error code is RPC_OK.
 * 
 * Also, note that an RPC_ETIMEOUT error indicates the request *may*
 * have been received and possibly processed by the server and that
 * an RPC_ERECVWAIT error indicates the request was *likely* received
 * and processed before generating a partial response.  Unfortunately
 * neither case can be fully verified and thus you may or may not need
 * to retry the request.
 *
 * Finally, the protocol errors above and request result codes from
 * RPC servers are confused in nfs/sob code the past. 
 * 
 */

#define RPC_OK		0
#define RPC_ENOCONN	(-1)
#define RPC_EHTTPSIZE	(-2)
#define RPC_EHTTPPARSE	(-3)
#define RPC_ESEND	(-4)
#define RPC_ERECV	(-5)
#define RPC_ESENDWAIT	(-6)
#define RPC_ERECVWAIT	(-7)
#define RPC_ETIMEOUT	(-8)

/*
 * Backwards compatible error/result codes for Dci_RpcTimedSend.
 */

#define RPC_NOCONN	(-1)
#define RPC_TIMEOUT	(-2)
#define RPC_ERROR	(-3)

#define RPC_STATE_GET	0
#define RPC_STATE_SEND	1
#define RPC_STATE_WAIT	2
#define RPC_STATE_RECV	3
#define RPC_NUM_STATES	4

#define DCI_SERVER_INIT 0
#define DCI_SERVER_DROP 1
#define DCI_SERVER_RECV 2
#define DCI_SERVER_SEND 3

#define DCI_SOCK_CLIENT 0
#define DCI_SOCK_SERVER 1

/*
 * RPC command code.
 */

#define DCI_NFSCMDREAD		1
#define DCI_NFSCMDPUT		2
#define DCI_NFSCMDUNLINK	3
#define DCI_NFSCMDCOPY		4
#define DCI_NFSCMDAPPEND	5
#define DCI_NFSCMDCBPOST	10
#define DCI_NFSCMDCBDEL		11

#define DCI_RPCNAMESIZE		32

extern char *dciDir;

typedef struct _Dci_Rpc *Dci_Rpc;
typedef struct _Dci_Msg *Dci_Msg;
typedef struct _Dci_Broadcaster *Dci_Broadcaster;
typedef struct _Dci_Log *Dci_Log;
typedef struct _Dci_Client *Dci_Client;
typedef struct _Dci_Board *Dci_Board;

typedef struct Dci_RpcJob {
    Dci_Rpc *rpc;	/* RPC server to send request. */
    void *data;		/* Client data (ignored by Dci_RpcRun). */
    int cmd;		/* Command code for request. */
    int result;		/* Response result code (valid if status is ok). */
    int error;		/* One of RPC_E error code for request. */
    Ns_DString *inPtr;	/* Data to send to server. */
    Ns_DString *outPtr; /* Data received in response. */
    Ns_Time	tstart;	/* Start time of job. */
    Ns_Time	tsend;	/* Time for first send. */
    Ns_Time	twait;	/* Time for send complete waiting for response. */
    Ns_Time	trecv;	/* Time for first receive. */
    Ns_Time	tdone;	/* Time receive done, request complete. */
} Dci_RpcJob;

typedef struct Dci_Elem {
    char  *key;
    char  *value;
} Dci_Elem;

typedef struct Dci_List {
    int	      nelem;
    Dci_Elem *elems;
} Dci_List;

#define Dci_ListKey(lp,i)	((lp)->elems[(i)].key)
#define Dci_ListValue(lp,i)	((lp)->elems[(i)].value)

typedef int (Dci_RpcProc)(void *arg, int cmd, Ns_DString *in, Ns_DString *out);
typedef int (Dci_RecvProc)(void *arg, Ns_DString *dsPtr);
typedef int (Dci_ServerProc)(Dci_Client *cPtr, void *serverData, int why);
typedef void (Dci_ListenAcceptProc) (int sock, void *arg);
typedef void (Dci_ListenExitProc) (void *arg);
typedef Dci_Msg *(Dci_MsgProc)(void *clientData);

extern Tcl_AppInitProc Dci_Init;
extern void Dci_Main(int argc, char **argv, Tcl_AppInitProc *proc);
extern Tcl_AppInitProc Dci_Init;
extern void Dci_LogIdent(char *module, char *ident);

/*
 * list.c
 */

extern int Dci_ListInit(Dci_List *listPtr, char *list);
extern Dci_Elem *Dci_ListSearch(Dci_List *listPtr, char *key);
extern void Dci_ListDump(Tcl_Interp *interp, Dci_List *listPtr, char *pattern,
			 int values);
extern void Dci_ListFree(Dci_List *listPtr);

/*
 * compat30.c
 */

extern int DciWriteChan(Tcl_Interp *interp, char *chanId, char *buf, int len);

/*
 * cb.c
 */

extern Dci_Board *Dci_CbParse(char *msgs);
extern char *Dci_CbGet(Dci_Board *board, int first, int last, int *npostsPtr);
extern int Dci_CbPost(char *logid, Ns_DString *dsPtr, char *user, char *msg,
		      char *fields, int maxposts);
extern int Dci_CbDelete(char *logid, Ns_DString *dsPtr, int id);
extern void Dci_CbFree(Dci_Board *board);

/*
 * page.c
 */

extern char * Dci_GetPageVar(Tcl_Interp *interp, char *varName);
extern char * Dci_SetPageVar(Tcl_Interp *interp, char *varName, char *value);

/*
 * rpc.c
 */

extern int Dci_RpcName(char *class, char *instance, char *buf);
extern Dci_Rpc *Dci_RpcCreateClient(char *server, char *module, char *name, int timeout);
extern int Dci_RpcCreateServer(char *server, char *module, char *name,
		char *handshake, Ns_Set *clients, Dci_RpcProc *proc, void *arg);
extern int Dci_RpcSend(Dci_Rpc *rpc, int cmd, Ns_DString *inPtr,
		       Ns_DString *outPtr);
extern int Dci_RpcTimedSend(Dci_Rpc *rpc, int cmd, Ns_DString *inPtr,
			    Ns_DString *outPtr, int reqtimeout);
extern void Dci_RpcRun(Dci_RpcJob *jobs, int njob, int timeout, int flags);
extern int Dci_NcfSend(char *cache, char *key);
extern char *Dci_RpcTclError(Tcl_Interp *interp, int err);

/*
 * sob.c
 */

extern int Dci_SobRpcName(char *name, char *buf);

/*
 * sock.c
 */

extern int Dci_ListenCallback(char *name, char *addr, int port,
			      Dci_ListenAcceptProc *acceptProc,
			      Dci_ListenExitProc *exitProc, void *arg);

extern int Dci_GetUuid(char *buf);

/*
 * server.c
 */

extern int Dci_CreateServer(char *server, void *serverData, Ns_Set *clients,
			    Dci_ServerProc *procPtr);
extern int Dci_StopServer(char *server);
extern char *Dci_ServerGetName(Dci_Client *cPtr);
extern void *Dci_ServerGetData(Dci_Client *cPtr);
extern void Dci_ServerSetData(Dci_Client *cPtr, void *dataPtr);
extern void Dci_ServerSend(Dci_Client *cPtr, void *bufPtr, int len);
extern void Dci_ServerSendVec(Dci_Client *cPtr, struct iovec *iov, int iovcnt);
extern void Dci_ServerRecv(Dci_Client *cPtr, void *bufPtr, int len);

/*
 * receiver.c
 */

extern int Dci_CreateReceiver(char *name, Dci_RecvProc *proc, void *arg);

/*
 * broadcast.c
 */

extern Dci_Broadcaster *Dci_CreateBroadcaster(char *name, void *clientData,
					     Ns_Set *clients,
					     Dci_MsgProc *initProc);
extern Dci_Msg *Dci_MsgAlloc(size_t size);
extern void Dci_MsgDecr(Dci_Msg *msgPtr);
extern void *Dci_MsgData(Dci_Msg *msgPtr);
extern void Dci_Broadcast(Dci_Broadcaster *broadcaster, Dci_Msg *msgPtr);

extern int Dci_MkDirs(char *dir, int mode);
extern int Dci_AdpPuts(Tcl_Interp *interp, char *html);
extern int Dci_AdpEval(Tcl_Interp *interp, char *script);
extern int Dci_AdpSafeEval(Tcl_Interp *interp, char *script);

extern void Dci_LogCommon(char *log, Ns_Conn *conn, char *request);
extern int Dci_ArtGetDims(Tcl_Interp *interp, char *group, char *name,
			  char *wbuf, char *hbuf);
extern char *Dci_ArtGetUrl(Ns_DString *dsPtr, char *group, char *name);

/*
 * datenum.c
 */

extern int Dci_Time2Dn(time_t time);
extern int Dci_Date2Dn(int yr, int mo, int day);
extern void Dci_Dn2Date(int dn, int *yrPtr, int *moPtr, int *dayPtr);
extern int Dci_Dn2Dow(int dn);
extern size_t Dci_Strftime(char *buf, size_t size, const char *fmt, time_t time);
extern Tcl_CmdProc DciSetDebugCmd;

/*
 * misc.c
 */
 
extern void Dci_SetIntResult(Tcl_Interp *interp, int i);
extern void Dci_FindTagAtts(Ns_Set *setPtr, char *start, size_t len);

#endif
