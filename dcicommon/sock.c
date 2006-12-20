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

typedef struct {
    Dci_ListenAcceptProc *acceptProc;
    Dci_ListenExitProc *exitProc;
    void *arg;
    char name[1];
} ListenData;


void
DciSockLibInit(void)
{
    DciAddIdent(rcsid);
}


static int
ListenProc(int sock, void *arg, int why)
{
    ListenData *lPtr = arg;

    if (why == NS_SOCK_EXIT) {
	Ns_Log(Notice, "%s: shutdown pending", lPtr->name);
	if (lPtr->exitProc != NULL) {
	    (*lPtr->exitProc)(lPtr->arg);
	}
	close(sock);
	Ns_Log(Notice, "%s: shutdown complete", lPtr->name);
	ns_free(lPtr);
	return NS_FALSE;
    }

    sock = Ns_SockAccept(sock, NULL, NULL);
    if (sock == -1) {
	Ns_Log(Error, "%s: accept() failed: %s", lPtr->name, strerror(errno));
    } else {
	DciLogPeer2(sock, lPtr->name, "connected");
	(lPtr->acceptProc)(sock, lPtr->arg);
    }
    return NS_TRUE;
}


int
Dci_ListenCallback(char *name, char *addr, int port,
	Dci_ListenAcceptProc *acceptProc, Dci_ListenExitProc *exitProc, void *arg)
{
    ListenData *lPtr;
    int sock;
    int ok;

    lPtr = ns_malloc(sizeof(ListenData) + strlen(name));
    lPtr->acceptProc = acceptProc;
    lPtr->exitProc = exitProc;
    lPtr->arg = arg;
    strcpy(lPtr->name, name);
    ok = 0;
    sock = Ns_SockListen(addr, port);
    if (sock != -1) {
	Ns_SockSetNonBlocking(sock);
	if (Ns_SockCallback(sock, ListenProc, lPtr, NS_SOCK_READ|NS_SOCK_EXIT) != NS_OK) {
	    Ns_Log(Error, "Dci_ListenCallback: %s: %s(%d): %s", name, addr, port, strerror(errno));
	    close(sock);
	} else {
	    ok = 1;
	}
    }
    Ns_Log(ok ? Notice : Error, "%s: %s on %s:%d", name,
	   ok ? "listening" : "could not listen", addr ? addr : "*", port);
    if (!ok) {
	ns_free(lPtr);
    }
    return (ok ? NS_OK : NS_ERROR);
}


char *
DciGetPeer(int sock)
{
    struct sockaddr_in sa;
    int len;

    len = sizeof(sa);
    if (getpeername(sock, (struct sockaddr *) &sa, &len) != 0) {
        return ns_sockstrerror(ns_sockerrno);
    }
    return ns_inet_ntoa(sa.sin_addr);
}


void
DciLogPeer2(int sock, char *ident, char *msg)
{
    Ns_Log(Notice, "[%s]: %s: %s", ident, msg, DciGetPeer(sock));
}

void
DciLogPeer(int sock, char *msg)
{
    Ns_Log(Notice, "%s: %s", msg, DciGetPeer(sock));
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_UpdateIov --
 *
 *      Modify the iovec struct iov to reflect a partial sendmsg.
 *      This enables subsequent calls to sendmsg to send only
 *      the data that was not previously sent.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates iov_base and iov_len members of iov.
 *
 *----------------------------------------------------------------------
 */

void
Dci_UpdateIov(struct iovec *iov, int iovcnt, int n)
{
    int i;
    
    for (i = 0; i < iovcnt && n > 0; ++i) {
    	if (iov[i].iov_len > 0) {
	    if (n >= iov[i].iov_len) {
		n -= iov[i].iov_len;
		iov[i].iov_len = 0;
		iov[i].iov_base = (caddr_t) NULL;
	    } else {
    		iov[i].iov_len -= n;
		iov[i].iov_base = (caddr_t) iov[i].iov_base + n;
		n = 0;
	    }
	}
    }
}


static void
SetOpt(int sock, int level, int name, int value)
{
    if (setsockopt(sock, level, name, (char *) &value, sizeof(value)) != 0) {
	Ns_Log(Error, "setsockopt(%d, %d, %d, %d) failed: %s",
	    sock, level, name, value, strerror(errno));
    }
}


void
Dci_SockOpts(int sock, int type)
{
    SetOpt(sock, IPPROTO_TCP, TCP_NODELAY, 1);
    if (type == DCI_SOCK_SERVER) {
    	SetOpt(sock, SOL_SOCKET, SO_KEEPALIVE, 1);
#ifdef TCL_KEEPALIVE
    	SetOpt(sock, IPPROTO_TCP, TCP_KEEPALIVE, 300); /* NB: 5 minutes. */
#endif
    }
}
