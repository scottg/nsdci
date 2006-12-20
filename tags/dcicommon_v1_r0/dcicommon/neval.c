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

#define MAXPASS 8

typedef struct Eval {
    struct Eval *nextPtr;
    int sock;
    char peer[20];
} Eval;

static Eval *firstFreePtr;
static Eval *lastEvalPtr;
static Eval *firstEvalPtr;

static Dci_ListenAcceptProc EvalAccept;
static Dci_ListenExitProc EvalExit;
static Ns_ThreadProc EvalThread;
static void EvalRespond(int sock, int code, Tcl_Interp *interp);
static int EvalSend(int sock, void *buf, int len);
static int EvalRecv(int sock, void *buf, int len);
static int EvalRecvReq(int sock, Tcl_Interp *interp, Ns_DString *dsPtr);
static void EvalDenied(int sock, Tcl_Interp *interp);
static char *password;
static Tcl_HashTable allowed;
static Ns_Thread evalThread;
static int shutdownPending;
static Ns_Mutex lock;
static Ns_Cond cond;
static Tcl_CmdProc SendCmd;
static Ns_TclInterpInitProc AddCmds;

int
DciNetEvalInit(char *server, char *module)
{
    char *addr, *path, *peers, *p, *sep;
    Eval *ePtr;
    int port, max, new;

    Tcl_InitHashTable(&allowed, TCL_STRING_KEYS);
    Dci_LogIdent(module, rcsid);
    path = Ns_ConfigGetPath(server, module, "neval", NULL);
    addr = Ns_ConfigGetValue(path, "address");
    if (!Ns_ConfigGetInt(path, "maxqueue", &max) || max <= 0) {
	max = 5;
    }
    password = Ns_ConfigGetValue(path, "password");
    peers = Ns_ConfigGetValue(path, "allowed");
    if (password == NULL || peers == NULL || 
    	!Ns_ConfigGetInt(path, "port", &port)) {
	Ns_Log(Notice, "neval: required password/allowed/port config missing");
    } else {
	p = peers = ns_strdup(peers);
	do {
	    sep = strchr(p, ',');
	    if (sep != NULL) {
		*sep++ = '\0';
	    }
	    p = Ns_StrTrim(p);
	    (void) Tcl_CreateHashEntry(&allowed, p, &new);
	    p = sep;
	} while (p != NULL);
	ns_free(peers);
	if (Dci_ListenCallback("neval", addr, port, EvalAccept, EvalExit, server) != NS_OK) {
	    return NS_ERROR;
	}
	while (max-- > 0) {
	    ePtr = ns_malloc(sizeof(Eval));
	    ePtr->nextPtr = firstFreePtr;
	    firstFreePtr = ePtr;
	}
    }
    Ns_TclInitInterps(server, AddCmds, NULL);
    return NS_OK;
}


static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "neval.send", SendCmd, NULL, NULL);
    return TCL_OK;
}


static int
SendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int sock;
    int code, slen, port, ok, timeout;
    char *host, *script, pass[MAXPASS+1];
    Ns_DString ds;
    uint32_t ulen;
    
    if (argc != 5 && argc != 6) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " host port pass script ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &port) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (argc == 5) {
	timeout = 5;
    } else if (Tcl_GetInt(interp, argv[5], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    host = argv[1];
    sock = Ns_SockTimedConnect(host, port, timeout);
    if (sock < 0) {
    	Tcl_AppendResult(interp, "could not connect to \"",
	    host, ":", argv[2], "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    script = argv[4];
    slen = strlen(script);
    strncpy(pass, argv[3], MAXPASS);
    pass[MAXPASS] = '\0';
    ulen = htonl(slen);
    ok = 0;
    Ns_Log(Notice, "neval: send to %s:%d: {%s}", host, port, script);
    Ns_DStringInit(&ds);
    code = TCL_OK;
    if (EvalSend(sock, &ulen, sizeof(ulen)) &&
    	EvalSend(sock, pass, MAXPASS) &&
    	EvalSend(sock, script, slen) &&
	Ns_SockWait(sock, NS_SOCK_READ, timeout) == NS_OK &&
	EvalRecv(sock, ds.string, ds.length)) {
	ok = DciTclRecv(sock, &ds, timeout);
	if (ok) {
	    code = DciTclImport(interp, &ds, DCI_EXPORTFMT_NEVAL);
	}
    }
    Ns_DStringFree(&ds);
    close(sock);
    if (!ok) {
    	Tcl_SetResult(interp, "could not send request", TCL_STATIC);
	return TCL_ERROR;
    }
    return code;
}


static void
EvalExit(void *ignored)
{
    Eval *ePtr;

    Ns_MutexLock(&lock);
    if (evalThread != NULL) {
	shutdownPending = 1;
	Ns_CondSignal(&cond);
	while (shutdownPending) {
	    Ns_CondWait(&cond, &lock);
	}
	Ns_ThreadJoin(&evalThread, NULL);
	evalThread = NULL;
    }
    while ((ePtr = firstFreePtr) != NULL) {
	firstFreePtr = ePtr->nextPtr;
	ns_free(ePtr);
    }
    Ns_MutexUnlock(&lock);
}


static void
EvalAccept(int sock, void *arg)
{
    Tcl_Interp *interp;
    char *server = arg;
    char *peer;
    Eval *ePtr;

    peer = DciGetPeer(sock);
    if (Tcl_FindHashEntry(&allowed, peer) == NULL) {
	DciLogPeer2(sock, "neval", "disallowed peer");
	interp = Ns_TclAllocateInterp(server);
	EvalDenied(sock, interp);
	Ns_TclDeAllocateInterp(interp);
    } else {
	Ns_MutexLock(&lock);
	ePtr = firstFreePtr;
	if (ePtr != NULL) {
    	    ePtr->sock = sock;
	    strcpy(ePtr->peer, peer);
	    firstFreePtr = ePtr->nextPtr;
	    ePtr->nextPtr = NULL;
	    if (firstEvalPtr == NULL) {
		firstEvalPtr = ePtr;
	    } else {
		lastEvalPtr->nextPtr = ePtr;
	    }
	    lastEvalPtr = ePtr;
	    DciLogPeer2(sock, "neval", "queued");
	    Ns_CondSignal(&cond);
	}
	if (evalThread == NULL) {
	    Ns_ThreadCreate(EvalThread, server, 0, &evalThread);
	}
	Ns_MutexUnlock(&lock);
	if (ePtr == NULL) {
	    interp = Ns_TclAllocateInterp(server);
	    Tcl_SetResult(interp, "server busy", TCL_STATIC);
	    EvalRespond(sock, TCL_ERROR, interp);
	    Ns_TclDeAllocateInterp(interp);
	    DciLogPeer2(sock, "neval", "dropped");
	}
    }
}


static void
EvalThread(void *arg)
{
    Tcl_Interp *interp;
    Eval *ePtr;
    int code;
    char *server = arg;
    Ns_DString ds;
    
    Ns_ThreadSetName("-neval-");
    Ns_DStringInit(&ds);
    Ns_MutexLock(&lock);
    while (!shutdownPending) {
	while (firstEvalPtr == NULL && !shutdownPending) {
	    Ns_CondWait(&cond, &lock);
	}
	if (shutdownPending) {
	    break;
	}
	ePtr = firstEvalPtr;
	firstEvalPtr = ePtr->nextPtr;
	if (lastEvalPtr == ePtr) {
	    lastEvalPtr = NULL;
	}
	Ns_MutexUnlock(&lock);
	interp = Ns_TclAllocateInterp(server);
	if (EvalRecvReq(ePtr->sock, interp, &ds)) {
	    Ns_Log(Notice, "recv from %s: {%s}", ePtr->peer, ds.string);
	    code = Tcl_Eval(interp, ds.string);
	    EvalRespond(ePtr->sock, code, interp);
	}
	Ns_TclDeAllocateInterp(interp);
        Ns_DStringFree(&ds);
	Ns_MutexLock(&lock);
	ePtr->nextPtr = firstFreePtr;
	firstFreePtr = ePtr;
    }
    shutdownPending = 0;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(&ds);
}


static int
EvalRecvReq(int sock, Tcl_Interp *interp, Ns_DString *dsPtr)
{
    char pass[MAXPASS+1], encpass[MAXPASS+1];
    uint32_t ulen;
    int len;
    
    if (EvalRecv(sock, &ulen, sizeof(ulen)) &&
	EvalRecv(sock, pass, MAXPASS)) {
	pass[MAXPASS] = '\0';
	Ns_Encrypt(pass, password, encpass);
	if (!STREQ(password, encpass)) {
	    EvalDenied(sock, interp);
	    return 0;
	}
	len = ntohl(ulen);
	Ns_DStringSetLength(dsPtr, len);
	if (EvalRecv(sock, dsPtr->string, len)) {
	    return 1;
	}
    }
    DciLogPeer2(sock, "neval", "recv() failed");
    close(sock);
    return 0;
}


static void
EvalRespond(int sock, int code, Tcl_Interp *interp)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    DciTclExport(interp, code, &ds, DCI_EXPORTFMT_NEVAL);
    if (!EvalSend(sock, ds.string, ds.length)) {
	DciLogPeer2(sock, "neval", "response failed - send() error");
    } else {
	DciLogPeer2(sock, "neval", "response ok");
    }
    close(sock);
    Ns_DStringFree(&ds);
}


static void
EvalDenied(int sock, Tcl_Interp *interp)
{
    Tcl_SetResult(interp, "access denied", TCL_STATIC);
    EvalRespond(sock, TCL_ERROR, interp);
}


static int
EvalRecv(int sock, void *buf, int len)
{
    char *p = (char *) buf;
    int n;

    while (len > 0) {
	n = Ns_SockRecv(sock, p, len, 1);
	if (n <= 0) {
	    return 0;
	}
	len -= n;
	p += n;
    }
    return 1;
}


static int
EvalSend(int sock, void *buf, int len)
{
    char *p = (char *) buf;
    int n;

    while (len > 0) {
	n = Ns_SockSend(sock, p, len, 1);
	if (n < 0) {
	    return 0;
	}
	len -= n;
	p += n;
    }
    return 1;
}
