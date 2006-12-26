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

typedef struct Recv {
    Dci_RecvProc *proc;
    void *arg;
} Recv;

/*
 * The following structure maintains context for each socket
 * connected to a client end point.
 */

typedef struct Sock {
    Recv *recvPtr;	    /* Receiver to service. */
    int readlen;    	    /* 1/0 for reading length or msg. */
    uint32_t len;   	    /* Message length. */
    char *next;     	    /* Next input pointer in len or ds. */
    int   nleft;    	    /* Bytes left to read after next. */
    Ns_DString ds;  	    /* Message buffer. */
} Sock;

static Dci_ListenAcceptProc RecvAccept;
static int RecvMsg(int sock, void *arg, int why);
static void ResetSock(Sock *sockPtr);
static Tcl_HashTable receivers;


/*
 *----------------------------------------------------------------
 *
 * DciReceiverInit --
 *
 *      Initialize the receiver, listening for logins on
 *	configured address/port.
 *
 * Results:
 *      Normally NS_OK.
 *
 * Side effects;
 *      May listen for logins.
 *
 *----------------------------------------------------------------
 */

int
DciReceiverInit(char *server, char *module)
{
    char *path, *addr;
    int port;

    Dci_LogIdent(module, rcsid);
    Tcl_InitHashTable(&receivers, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "receiver", NULL);
    addr = Ns_ConfigGetValue(path, "address");
    if (!Ns_ConfigGetInt(path, "port", &port)) {
	return NS_OK;
    }
    return Dci_ListenCallback("receiver", addr, port, RecvAccept, NULL, NULL);
}
    

/*
 *----------------------------------------------------------------
 *
 * Dci_CreateReceiver --
 *
 *      Create a new receiver which will invoke the given
 *	callback for broadcasters logged in with given login.
 *
 * Results:
 *      None.
 *
 * Side effects;
 *      Next reads will be part of message length.
 *
 *----------------------------------------------------------------
 */


int
Dci_CreateReceiver(char *login, Dci_RecvProc *proc, void *arg)
{
    Tcl_HashEntry *hPtr;
    Recv *recvPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&receivers, login, &new);
    if (!new) {
	Ns_Log(Notice, "receiver: duplicate login: %s", login);
	return NS_ERROR;
    }
    recvPtr = ns_malloc(sizeof(Recv));
    recvPtr->proc = proc;
    recvPtr->arg = arg;
    Tcl_SetHashValue(hPtr, recvPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 *
 * ResetSock --
 *
 *      Reset a client socket to the initial message read state.
 *
 * Results:
 *      None.
 *
 * Side effects;
 *      Next reads will be part of message length.
 *
 *----------------------------------------------------------------
 */

static void
ResetSock(Sock *sockPtr)
{
    sockPtr->readlen = 1;
    sockPtr->next = (char *) &sockPtr->len;
    sockPtr->nleft = sizeof(sockPtr->len);
    Ns_DStringFree(&sockPtr->ds);
}


/*
 *----------------------------------------------------------------
 *
 * RecvAccept --
 *
 *      Dci sock listen callback to accept an incoming client
 *	connection.
 *
 * Results:
 *      None.
 *
 * Side effects;
 *      New Sock context is allocated and initialized for
 *	message recv.
 *
 *----------------------------------------------------------------
 */

static void
RecvAccept(int sock, void *arg)
{
    Sock *sockPtr;

    sockPtr = ns_malloc(sizeof(Sock));
    sockPtr->recvPtr = NULL;
    Ns_DStringInit(&sockPtr->ds);
    ResetSock(sockPtr);
    Ns_SockCallback(sock, RecvMsg, sockPtr, NS_SOCK_READ | NS_SOCK_EXIT);
}


/*
 *----------------------------------------------------------------
 *
 * RecvMsg --
 *
 *      Process messages from broadcaster, logging in to a receiver
 *	on the first message.
 *
 * Results:
 *      NS_TRUE if more data should be waited for, NS_FALSE
 *	on exit or error after socket has been closed.
 *
 * Side effects;
 *      Depends on callback.
 *
 *----------------------------------------------------------------
 */

static int
RecvMsg(int sock, void *arg, int why)
{
    Tcl_HashEntry *hPtr;
    Sock *sockPtr = arg;
    int n;

    if (why == NS_SOCK_EXIT) {
stop:
	Ns_DStringFree(&sockPtr->ds);
	ns_free(sockPtr);
        close(sock);
        return NS_FALSE;
    }

    /*
     * Read waiting data.
     */

    n = recv(sock, sockPtr->next, (size_t)sockPtr->nleft, 0);
    if (n <= 0) {
        if (n < 0) {
            Ns_Log(Error, "receiver: recv() failed: %s", strerror(errno));
	}
        DciLogPeer2(sock, "receiver", "dropped");
	goto stop;
    }

    /*
     * Adjust client read buffer.
     */

    sockPtr->next += n;
    sockPtr->nleft -= n;
    if (sockPtr->nleft == 0) {
	if (sockPtr->readlen) {
	    sockPtr->len = ntohl(sockPtr->len);
	    Ns_DStringSetLength(&sockPtr->ds, (int)sockPtr->len);
	    sockPtr->next = sockPtr->ds.string;
	    sockPtr->nleft = sockPtr->ds.length;
	    sockPtr->readlen = 0;
	} else {
	    if (sockPtr->recvPtr == NULL) {
		hPtr = Tcl_FindHashEntry(&receivers, sockPtr->ds.string);
		if (hPtr == NULL) {
		    Ns_Log(Error, "receiver: invalid login: %s", sockPtr->ds.string);
		    goto stop;
		}
		Ns_Log(Notice, "receiver: login: %s", sockPtr->ds.string);
		sockPtr->recvPtr = Tcl_GetHashValue(hPtr);
	    } else if ((*sockPtr->recvPtr->proc)(sockPtr->recvPtr->arg, &sockPtr->ds) != NS_OK) {
	    	goto stop;
	    }
	    ResetSock(sockPtr);
	}
    }
    return NS_TRUE;
}
