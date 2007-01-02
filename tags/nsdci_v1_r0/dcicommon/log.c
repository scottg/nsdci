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

static char *logDir;
static Ns_Cache *cache;
static Ns_TclInterpInitProc AddCmds;
static Tcl_CmdProc DirCmd;
static Tcl_CmdProc FileCmd;
static Tcl_CmdProc RollCmd;
static Tcl_CmdProc AppendCmd;
static Tcl_CmdProc LogCommonCmd;


int
DciLogInit(char *server, char *module)
{
    Ns_DString ds;
    int maxfds;
    char *path;

    Dci_LogIdent(module, rcsid);
    Ns_DStringInit(&ds);
    path = Ns_ConfigGetPath(server, module, "log", NULL);
    logDir = Ns_ConfigGetValue(path, "dir");
    if (logDir == NULL) {
	logDir = Ns_HomePath(&ds, "log", NULL);
    }
    if (mkdir(logDir, 0755) != 0 && errno != EEXIST) {
	Ns_Log(Error, "log: could not create: %s", logDir); 
	return NS_ERROR;
    }
    logDir = ns_strdup(logDir);
    if (!Ns_ConfigGetInt(path, "timeout", &maxfds) || maxfds < 1) {
	maxfds = 30;
    }
    cache = Ns_CacheCreateSz("logs", TCL_STRING_KEYS, (size_t)maxfds, (Ns_Callback *) close);
    Ns_TclInitInterps(server, AddCmds, NULL);
    return TCL_OK;
}


static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "log.dir", DirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.file", FileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.append", AppendCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.roll", RollCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.common", LogCommonCmd, NULL, NULL);
    return TCL_OK;
}


int
DciAppendLog(char *log, char *string)
{
    int ok, fd, new, len;
    Ns_Entry *entry;
    Ns_DString ds;
    char *newline = "\n";
    struct iovec iov[3];
    int iovcnt;
    char timestamp[20];
    time_t now;
    static time_t nextLog;

    ok = 0;
    len =strlen(string);
    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, logDir, log, NULL);
    sprintf(timestamp, "%ld:", time(NULL));

    iovcnt = 2;
    iov[0].iov_base = timestamp;
    iov[0].iov_len = strlen(timestamp);
    iov[1].iov_base = string;
    iov[1].iov_len = len;
    if (len < 1 || string[len-1] != '\n') {
	iov[2].iov_base = newline;
	iov[2].iov_len = 1;
	++len;
	++iovcnt;
    }
    len += iov[0].iov_len;

    Ns_CacheLock(cache);
    entry = Ns_CacheCreateEntry(cache, log, &new);
    if (!new) {
	fd = (int) Ns_CacheGetValue(entry);
    } else {
	fd = open(ds.string, O_WRONLY|O_APPEND|O_CREAT, 0644);
	if (fd < 0) {
	    now = time(NULL);
	    if (nextLog < now) {
		nextLog = now + 60;	/* Log no more than once per second. */
		Ns_Log(Error, "log: could not open %s: %s", ds.string, strerror(errno));
	    }
	    Ns_CacheDeleteEntry(entry);
	    goto unlock;
	}
	Ns_CacheSetValueSz(entry, (void *) fd, 1);
    }
    if (writev(fd, iov, iovcnt) != len) {
	goto unlock;
    }
    ok = 1;
unlock:
    Ns_CacheUnlock(cache);

    Ns_DStringFree(&ds);
    return ok;
}


static int
FileCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " log\"", NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Tcl_SetResult(interp, Ns_MakePath(&ds, logDir, argv[1], NULL), TCL_VOLATILE);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


static int
DirCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, logDir, TCL_STATIC);
    return TCL_OK;
}


static int
RollCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    int max;
    Ns_Entry *entry;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " log ?max?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
	max = 999;
    } else if (Tcl_GetInt(interp, argv[2], &max) != TCL_OK) {
	return TCL_ERROR;

    } 

    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, logDir, argv[1], NULL);
    if (Ns_RollFile(ds.string, max) != NS_OK) {
	Tcl_AppendResult(interp, "could not roll \"",
	    ds.string, "\": ", Tcl_PosixError(interp), NULL);
	Ns_DStringFree(&ds);
	return TCL_ERROR;
    }
    Ns_DStringFree(&ds);

    Ns_CacheLock(cache);
    entry = Ns_CacheFindEntry(cache, argv[1]);
    if (entry != NULL) {
	Ns_CacheFlushEntry(entry);
    }
    Ns_CacheUnlock(cache);

    return TCL_OK;
}


static int
AppendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " log string\"", NULL);
	return TCL_ERROR;
    }

    if (!DciAppendLog(argv[1], argv[2])) {
	Tcl_AppendResult(interp, "could not append to log \"",
	    argv[1], "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


void
Dci_LogCommon(char *log, Ns_Conn *conn, char *request)
{
    Ns_DString line;
    register char *p;
    int quote, n;
    char buf[100];

    Ns_DStringInit(&line);

    /*
     * Append the peer address and auth user (if any).
     */

    Ns_DStringAppend(&line, Ns_ConnPeer(conn));
    if (!conn->authUser) {
    	Ns_DStringAppend(&line, " - - ");
    } else {
    	p = conn->authUser;
	quote = 0;
	while (*p) {
	    if (isspace(UCHAR(*p))) {
	    	quote = 1;
	    	break;
	    }
	    ++p;
    	}
	if (quote) {
    	    Ns_DStringVarAppend(&line, " - \"", conn->authUser, "\" ", NULL);
	} else {
    	    Ns_DStringVarAppend(&line, " - ", conn->authUser, " ", NULL);
	}
    }

    /*
     * Append a common log format time stamp including GMT offset.
     */

    Ns_LogTime(buf);
    Ns_DStringAppend(&line, buf);

    /*
     * Append the request line.
     */

    Ns_DStringVarAppend(&line, "\"", request, "\" ", NULL);

    /*
     * Construct and append the HTTP status code and bytes sent.
     */

    n = Ns_ConnResponseStatus(conn);
    sprintf(buf, "%d %u ", n ? n : 200, Ns_ConnContentSent(conn));
    Ns_DStringAppend(&line, buf);

    /*
     * Append the referer, user-agent and cookie headers (if any).
     */

    Ns_DStringAppend(&line, "\"");
    if ((p = Ns_SetIGet(conn->headers, "referer"))) {
	Ns_DStringAppend(&line, p);
    }
    Ns_DStringAppend(&line, "\" \"");
    if ((p = Ns_SetIGet(conn->headers, "user-agent"))) {
	Ns_DStringAppend(&line, p);
    }
    Ns_DStringAppend(&line, "\" \"");
    if ((p = Ns_SetIGet(conn->headers, "cookie"))) {
        Ns_DStringAppend(&line, p);
    }
    Ns_DStringAppend(&line, "\"\n");

    /*
     * Write the line to the log.
     */

    DciAppendLog(log, line.string);
    Ns_DStringFree(&line);
}


static int
LogCommonCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    char *method;
    Ns_Conn *conn;

    if (argc < 2 || argc > 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " log ?url? ?method?\"", NULL);
        return TCL_ERROR;
    }
    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
        Tcl_SetResult(interp, "no active connection", TCL_STATIC);
        return TCL_ERROR;
    }

    Ns_DStringInit(&ds);
    if (argc == 2) {
	Ns_DStringAppend(&ds, conn->request->line);
    } else {
	if (argc == 4) {
	    method = argv[3];
	} else {
	    method = conn->request->method;
	}
	Ns_DStringVarAppend(&ds, method, " ", argv[2], " HTTP/1.0", NULL);
    }

    Dci_LogCommon(argv[1], conn, ds.string);
    Ns_DStringFree(&ds);

    return TCL_OK;
}
