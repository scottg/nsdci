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

#define TRPC_EVAL 1

typedef struct Hdr {
    uint32_t	code;
    uint32_t	clen;
    uint32_t	ilen;
    uint32_t	rlen;
} Hdr;

#ifdef OBSOLETED
static void Init(Ns_DString *dsPtr, char **bufPtr, int *lenPtr);
static void Next(Ns_DString *dsPtr, char **bufPtr, int *lenPtr, int net);
#endif
static Tcl_CmdProc SendCmd;
static Ns_TclInterpInitProc AddCmds;
static Dci_RpcProc TclProc;
static int fDebug;


int
DciNetProxyInit(char *server, char *module)
{
    Dci_Rpc *rpc;
    Ns_Set *set, *clients;
    char *path, *name, *handshake;
    char hbuf[DCI_RPCNAMESIZE], rpcname[DCI_RPCNAMESIZE];
    int timeout, i, new;
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;

    Dci_LogIdent(module, rcsid);
    tablePtr = ns_malloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "nproxy", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
    	fDebug = 0;
    }

    path = Ns_ConfigGetPath(server, module, "nproxy/clients", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	name = Ns_SetKey(set, i);
	if (Dci_RpcName("nproxy", name, rpcname) != NS_OK) {
	    return NS_ERROR;
	}
    	path = Ns_ConfigGetPath(server, module, "nproxy/client", name, NULL);
	if (!Ns_ConfigGetInt(path, "timeout", &timeout)) {
	    timeout = 10;
	}
	hPtr = Tcl_CreateHashEntry(tablePtr, name, &new);
	if (!new) {
	    Ns_Log(Error, "tclc[%s]: multiply defined", name);
	    return NS_ERROR;
	}
	rpc = Dci_RpcCreateClient(server, module, rpcname, timeout);
	if (rpc == NULL) {
	    return NS_ERROR;
	}
	Tcl_SetHashValue(hPtr, rpc);
    }

    path = Ns_ConfigGetPath(server, module, "nproxy/servers", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	name = Ns_SetKey(set, i);
	if (Dci_RpcName("nproxy", name, rpcname) != NS_OK) {
	    return NS_ERROR;
	}
        
        path = Ns_ConfigGetPath(server, module, "nproxy/server",
		name, NULL);
        handshake = Ns_ConfigGetValue(path, "handshakeName");
        if (handshake == NULL) {
	    handshake = rpcname;
	} else {
	    if (Dci_RpcName("nproxy", handshake, hbuf) != NS_OK) {
            	return NS_ERROR;
	    }
	    handshake = hbuf;
        }
        Ns_Log(Notice, "nproxy[%s] connecting as %s", rpcname, handshake);
    	path = Ns_ConfigGetPath(server, module, "nproxy/server", name,
				"clients", NULL);
	clients = Ns_ConfigGetSection(path);
	if (clients == NULL) {
	    Ns_Log(Error, "tcls[%s]: missing clients section", name);
	    return NS_ERROR;
	}
    	if (Dci_RpcCreateServer(server, server, rpcname, handshake, clients,
				TclProc, server) != NS_OK) {
	    return NS_ERROR;
	}
    }
    Ns_TclInitInterps(server, AddCmds, tablePtr);
    return NS_OK;
}


static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nproxy.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nproxy.send", SendCmd, arg, NULL);
    return NS_OK;
}


#ifdef OBSOLETED
/*
 * These appear to be unused.  They are still here incase
 * a need for them reappears.
 */
static void
Init(Ns_DString *dsPtr, char **bufPtr, int *lenPtr)
{
    Hdr *hdrPtr;
    int len;

    len = sizeof(Hdr);
    Ns_DStringSetLength(dsPtr, len);
    hdrPtr = (Hdr *) dsPtr->string;
    memset(hdrPtr, 0, (size_t)len);
    *bufPtr = (char *) hdrPtr;
    *lenPtr = len;
}


static void
Next(Ns_DString *dsPtr, char **bufPtr, int *lenPtr, int net)
{
    Hdr *hdrPtr;
    int len;

    hdrPtr = (Hdr *) dsPtr->string;
    if (net) {
        len = ntohl(hdrPtr->rlen) + ntohl(hdrPtr->clen) + ntohl(hdrPtr->ilen);
    } else {
	len = hdrPtr->rlen + hdrPtr->clen + hdrPtr->ilen;
	hdrPtr->code = htonl(hdrPtr->code);
	hdrPtr->rlen = htonl(hdrPtr->rlen);
	hdrPtr->clen = htonl(hdrPtr->clen);
	hdrPtr->ilen = htonl(hdrPtr->ilen);
    }
    Ns_DStringSetLength(dsPtr, (int)(sizeof(Hdr) + len));
    *bufPtr = dsPtr->string + sizeof(Hdr);
    *lenPtr = len;
}
#endif

static int
SendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc;
    Ns_DString in, out;
    int err, timeout, code;
    Tcl_HashTable *tablePtr = arg;
    Tcl_HashEntry *hPtr;

    if (argc != 3 && argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server script ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3) {
	timeout = 2;
    } else if (Tcl_GetInt(interp, argv[3], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    hPtr = Tcl_FindHashEntry(tablePtr, argv[1]);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such nproxy: ", argv[1], NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    rpc = Tcl_GetHashValue(hPtr);
    Ns_DStringAppend(&in, argv[2]);
    err = Dci_RpcTimedSend(rpc, TRPC_EVAL, &in, &out, timeout);
    if (err == RPC_OK) {
	code = DciTclImport(interp, &out, DCI_EXPORTFMT_NPROXY);
    } else {
	Tcl_AppendResult(interp, "could not send request: ", in.string, 
            " : ", Dci_RpcTclError(interp, err), NULL);
	code = TCL_ERROR;
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return code;
}


static int
TclProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    char *server = arg;
    Tcl_Interp *interp;
    int code;
    
    if (cmd != TRPC_EVAL) {
    	Ns_Log(Error, "invalid request");
    	return NS_ERROR;
    }
    interp = Ns_TclAllocateInterp(server);
    code = Tcl_Eval(interp, inPtr->string);
    if (fDebug) {
	Ns_Log(Notice, "eval: %d %.40s ...", code, inPtr->string);
    }
    DciTclExport(interp, code, outPtr, DCI_EXPORTFMT_NPROXY);
    Ns_TclDeAllocateInterp(interp);
    return NS_OK;
}
