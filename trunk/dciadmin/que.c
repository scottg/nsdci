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

#include "dciadmin.h"
#include <sys/stat.h>

#define BUF_SIZE 8192

typedef struct {
    char code[2];
    short length;
    char buf[BUF_SIZE];
} RMMsg;

static int RMWait(Tcl_Interp *interp, int sock, int timeout, int write);
static int RMRecv(Tcl_Interp *interp, int sock, char *buf, int *len, int timeout);
static int RMSend(Tcl_Interp *interp, int sock, char *buf, int len, int timeout);
static Dci_Que *rmQue;

int
DciQueInit(char *server, char *module)
{
    static char script[] = "que.run";

    rmQue = DciQueCreate(server, "-que-", script);
    return NS_OK;
}


int
Dci_QueNextCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    int time;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " time\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &time) != TCL_OK) {
	return TCL_ERROR;
    }
    DciQueNext(rmQue, time);
    return TCL_OK;
}


int
Dci_QueSendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int fd, sock, port, timeout, n, result, total;
    RMMsg msg;
    struct stat st;
    
    if (argc != 4 && argc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " address port file ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &port) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc == 4) {
    	timeout = 10;
    } else if (Tcl_GetInt(interp, argv[4], &timeout) != TCL_OK) {
    	return TCL_ERROR;
    }
    fd = open(argv[3], O_RDONLY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"", 
	    argv[3], "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    if (fstat(fd, &st) != 0) {
	Tcl_AppendResult(interp, "fstat() failed: ", Tcl_PosixError(interp), NULL);
	close(fd);
	return TCL_ERROR;
    }
    sock = Ns_SockTimedConnect(argv[1], port, timeout);
    if (sock < 0) {
	Tcl_AppendResult(interp, "could not connect to \"",
	    argv[1], ":", argv[2], "\": ", Tcl_PosixError(interp), NULL);
	close(fd);
	return TCL_ERROR;
    }


    result = TCL_ERROR;
    n = BUF_SIZE;
    if (RMRecv(interp, sock, msg.buf, &n, timeout) != TCL_OK) {
	goto done;
    }
    if (strncmp("Welcome", msg.buf+4, 7) != 0) {
    	interp->result = "invalid welcome from RM";
	goto done;
    }
    msg.code[0] = '0';
    msg.code[1] = '1';
    total = 0;
    while (total < st.st_size) {
	n = read(fd, msg.buf, BUF_SIZE);
	if (n < 0) {
		Tcl_AppendResult(interp, "fstat() failed: ", Tcl_PosixError(interp), NULL);
		goto done;
	}
	total += n;
	msg.length = n;
    	if (RMSend(interp, sock, (char *) &msg, n + 4, timeout) != TCL_OK) {
    	    goto done;
    	}
    }
    msg.code[0] = '0';
    msg.code[1] = '9';
    msg.length = 4;
    memcpy(msg.buf, &total, 4);
    if (RMSend(interp, sock, (char *) &msg, 8, timeout) != TCL_OK) {
    	goto done;
    }
    n = 8;
    if (RMRecv(interp, sock, (char *) &msg, &n, timeout) != TCL_OK) {
    	goto done;
    }
    memcpy(&total, msg.buf, 4);
    if (total != st.st_size) {
	sprintf(interp->result, "%d bytes sent != %d bytes received", (int) st.st_size, total);
	goto done;
    }
    sprintf(interp->result, "%d", total);
    msg.code[0] = 0x01;
    msg.code[1] = '\0';
    if (RMSend(interp, sock, (char *) &msg, 2, timeout) != TCL_OK) {
    	goto done;
    }
    result = TCL_OK;
done:
    close(sock);
    close(fd);
    return result;
}


static int
RMWait(Tcl_Interp *interp, int sock, int timeout, int write)
{
    fd_set set;
    struct timeval tv;
    int n;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    if (write) {
	n = select(sock + 1, NULL, &set, NULL, &tv);
    } else {
	n = select(sock + 1, &set, NULL, NULL, &tv);
    }
    if (n < 0) {
	Tcl_AppendResult(interp, "select() failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    } else if (n == 0) {
	interp->result = "socket timeout";
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
RMSend(Tcl_Interp *interp, int sock, char *buf, int len, int timeout)
{
    int n;

    while (len > 0) {
	if (RMWait(interp, sock, timeout, 1) != TCL_OK) {
	    return TCL_ERROR;
	}
	n = send(sock, buf, len, 0); 
	if (n < 0) {
	    Tcl_AppendResult(interp, "send() failed: ", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}
	len -= n;
	buf += n;
    }
    return TCL_OK;
}


static int
RMRecv(Tcl_Interp *interp, int sock, char *buf, int *len, int timeout)
{
    int read;

    if (RMWait(interp, sock, timeout, 0) != TCL_OK) {
	return TCL_ERROR;
    }
    read = recv(sock, buf, *len, 0);
    if (read < 0) {
	Tcl_AppendResult(interp, "recv() failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    *len = read;
    return TCL_OK;
}
