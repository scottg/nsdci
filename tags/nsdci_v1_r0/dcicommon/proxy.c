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

typedef struct Proxy {
    struct Proxy *nextPtr;
    int pid;
    int pipe[2];
} Proxy;

typedef struct Pool {
    char  *name;
    Proxy *firstPtr;
    char  *init;
    int    refid;
} Pool;

static void SetVec(struct iovec *iov, int i, void *buf, int len);
static int StartProxy(Pool *, Proxy *);
static void StopProxy(Pool *, Proxy *);
static Tcl_CmdProc StartCmd, PoolsCmd, SendCmd;
static char *dcitcl;
static Ns_Mutex lock;
static Ns_Cond cond;
static Tcl_HashTable pools;


void
DciProxyLibInit(void)
{
    Ns_DString ds;

    DciAddIdent(rcsid);
    Ns_DStringInit(&ds);
    Ns_HomePath(&ds, "bin", "dcitclx", NULL);
    dcitcl = Ns_DStringExport(&ds);
    Ns_MutexSetName(&lock, "dci:proxy");
    Tcl_InitHashTable(&pools, TCL_STRING_KEYS);
}


int
DciProxyModInit(char *server, char *module)
{
    char *path, *exec;

    path = Ns_ConfigGetPath(server, module, "proxy", NULL);
    exec = Ns_ConfigGetValue(path, "dcitcl");
    if (exec != NULL) {
	dcitcl = exec;
    }
    return NS_OK;
}


int
DciProxyTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "proxy.pools", PoolsCmd, NULL, NULL); 
    Tcl_CreateCommand(interp, "proxy.start", StartCmd, NULL, NULL); 
    Tcl_CreateCommand(interp, "proxy.send", SendCmd, NULL, NULL); 
    return TCL_OK;
}


void
DciProxyMain(int argc , char **argv)
{
    Ns_DString ds;
    char *str, *active, *dots;
    int avail, code, n, max, len, rfd, wfd;
    struct iovec iov[7];
    Tcl_Interp *interp;

    /*
     * Before doing anything, get the pipe fd's out of the way, dup
     * stderr to stdout, and open stdin on /dev/null.
     */

    if ((rfd = dup(0)) < 2
	|| (wfd = dup(1)) < 2
	|| close(0) != 0
    	|| open("/dev/null", O_RDONLY) != 0
	|| close(1) != 0
	|| dup(2) != 1) {
	Ns_Fatal("proxy started weird");
	/* NOT REACHED */
	return;
    }

    Ns_Log(Notice, "starting");

    /*
     * Turns out there's some important side-effects that the
     * Tcl_FindExecutable api provides to the rest of the environment.
     * Most notably, the tcl_libPath was not getting set when we failed
     * to call it.
     */

    interp = Ns_TclCreateInterp();
    if (DciAppInit(interp) != TCL_OK) {
	goto done;
    }
    if (argc > 3 && argv[3][0] != '\0') {
	if (Tcl_EvalFile(interp, argv[3]) == TCL_OK) {
    	    Ns_Log(Notice, "sourced: %s", argv[3]);
	} else {
    	    Ns_Log(Error, "could not source: %s, (%s)", argv[3],
                   Tcl_GetStringResult(interp));
	}
    }
    active = NULL;
    if (argc > 4) {
	max = strlen(argv[4]) - 8;
	if (max > 0) {
	    active = argv[4];
	}
    }
    Ns_DStringInit(&ds);
    Ns_Log(Notice, "running");
    while (1) {
	avail = ds.spaceAvl - 1;
	SetVec(iov, 0, &len, sizeof(len));
	SetVec(iov, 1, ds.string, avail);

	/*
	 * Read until at least the length header arrives.
	 */

	do {
	    n = readv(rfd, iov, 2);
	    if (n <= 0) {
	    	goto done;
	    }
	    Dci_UpdateIov(iov, 2, n);
	} while (iov[0].iov_len > 0);

	/*
	 * Update the dstring first to the size
	 * already read and then to the total
	 * size required.  Two updates are needed
	 * to ensure a possible resize on the 
	 * second update has the correct number
	 * of bytes to copy.
	 */

	n = avail - iov[1].iov_len;
	Ns_DStringSetLength(&ds, n);
	Ns_DStringSetLength(&ds, len);

	/*
	 * Continue reading the script if necessary.
	 */

	len -= n;
	str = ds.string + n;
	while (len > 0) {
	    do {
	    	n = read(rfd, str, (size_t)len);
	    } while (n < 0 && errno == EINTR);
	    if (n <= 0) {
		goto done;
	    }
	    len -= n;
	    str += n;
	}

	/*
	 * Evaluate the script and, if necessary,
	 * extract and log the error details.
	 */

	if (active != NULL) {
	   len = ds.length;
	   if (len > max) {
		len = max;
		dots = " ...";
	   } else {
		dots = "";
	   }
	   sprintf(active, "{%.*s%s}", len, ds.string, dots);
	}
    	code = Tcl_Eval(interp, ds.string);
	if (active != NULL) {
	    active[0] = '\0';
	}
	Ns_DStringSetLength(&ds, 0);
	DciTclExport(interp, code, &ds, DCI_EXPORTFMT_NPROXY);
	str = ds.string;
	len = ds.length;
	while (len > 0) {
	    do {
	    	n = write(wfd, str, (size_t)len);
	    } while (n < 0 && errno == EINTR);
	    if (n < 0) {
		goto done;
	    }
	    str += n;
	    len -= n;
	}
    }
done:
    Ns_Log(Notice, "exiting");
    Ns_DStringFree(&ds);
}


static int
PoolsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    char *name, *pattern;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?pattern?\"", NULL);
	return TCL_ERROR;
    }
    pattern = argv[1];
    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&pools, &search);
    while (hPtr != NULL) {
	name = Tcl_GetHashKey(&pools, hPtr);
	if (pattern == NULL || Tcl_StringMatch(name, pattern)) {
	    Tcl_AppendElement(interp, name);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);
    return TCL_OK;
}


static int
StartCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Pool *poolPtr;
    Proxy *proxyPtr, *firstPtr;
    int new, nproxies;
    Tcl_HashEntry *hPtr;

    if (argc < 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " name num ?init?\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &nproxies) != TCL_OK) {
	return TCL_ERROR;
    }
    if (nproxies < 1) {
	Tcl_AppendResult(interp, "invalid # proxies: ", argv[2], NULL);
	return TCL_ERROR;
    }

    /*
     * Create the pool entry.
     */

    Ns_MutexLock(&lock); 
    hPtr = Tcl_CreateHashEntry(&pools, argv[1], &new);
    if (new) {
	poolPtr = ns_calloc(1, sizeof(Pool));
	poolPtr->name = Tcl_GetHashKey(&pools, hPtr);
    	Tcl_SetHashValue(hPtr, poolPtr);
    } else {
	poolPtr = Tcl_GetHashValue(hPtr);
	++poolPtr->refid;
    }
    firstPtr = poolPtr->firstPtr;
    poolPtr->firstPtr = NULL;
    Ns_MutexUnlock(&lock);

    /*
     * Stop old proxies (if any) and allocate new proxies.
     */

    while ((proxyPtr = firstPtr) != NULL) {
	firstPtr = proxyPtr->nextPtr;
	StopProxy(poolPtr, proxyPtr);
    }
    while (--nproxies >= 0) {
    	proxyPtr = ns_malloc(sizeof(Proxy));
	proxyPtr->pipe[0] = proxyPtr->pipe[1] = proxyPtr->pid = -1;
	proxyPtr->nextPtr = firstPtr;
	firstPtr = proxyPtr;
    }

    /*
     * Add new proxies to pool.
     */

    Ns_MutexLock(&lock);
    while ((proxyPtr = firstPtr) != NULL) {
	firstPtr = proxyPtr->nextPtr;
	proxyPtr->nextPtr = poolPtr->firstPtr;
	poolPtr->firstPtr = proxyPtr;
    }
    if (poolPtr->init) {
	ns_free(poolPtr->init);
    }
    if (argc == 5 || argc == 3) {
	poolPtr->init = NULL;
    } else {
	poolPtr->init = ns_strdup(argv[argc-1]);
    }
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);

    return TCL_OK;
}


static void
SetVec(struct iovec *iov, int i, void *buf, int len)
{
    iov[i].iov_base = (caddr_t) buf;
    iov[i].iov_len = len;
}


static int
SendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int seconds, n, ioc, refid, ok, towrite;
    int code, len;
    Pool *poolPtr;
    Proxy *proxyPtr, **nextPtrPtr;
    char *failed;
    struct iovec iov[5];
    Ns_DString ds;
    Tcl_HashEntry *hPtr;
    Ns_Time timeout;

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " pool script ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3) {
	seconds = 5;
    } else {
	if (Tcl_GetInt(interp, argv[3], &seconds) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (seconds < 1) {
	    seconds = 1;
	}
    }

    ok = 0;
    code = TCL_ERROR;
    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&pools, argv[1]);
    if (hPtr == NULL) {
        Tcl_AppendResult(interp, "no such proxy pool: ", argv[1], NULL);
    } else {
	poolPtr = Tcl_GetHashValue(hPtr);
	Ns_GetTime(&timeout);
	Ns_IncrTime(&timeout, seconds, 0);
	while (poolPtr->firstPtr == NULL) {
	    if (Ns_CondTimedWait(&cond, &lock, &timeout) != NS_OK) {
		break;
	    }
	}
	if (!poolPtr->firstPtr) {
	    Tcl_SetResult(interp, "timeout waiting for proxy", TCL_STATIC);
	} else {
	    refid = poolPtr->refid;
	    proxyPtr = poolPtr->firstPtr;
	    poolPtr->firstPtr = proxyPtr->nextPtr;
	    Ns_MutexUnlock(&lock);
    	    failed = "start";
	    if (proxyPtr->pid < 0) {
	    	if (!StartProxy(poolPtr, proxyPtr)) {
    	    	    goto done;
		}
	    }
    	    failed = "write";
	    ioc = 0;
	    len = strlen(argv[2]);
	    SetVec(iov, ioc++, &len, sizeof(int));
	    SetVec(iov, ioc++, argv[2], len);
	    towrite = 0;
	    for (n = 0; n < ioc; ++n) {
		towrite += iov[n].iov_len;
	    }
	    while (towrite > 0) {
		n = writev(proxyPtr->pipe[1], iov, ioc);
		if (n < 0) {
    		    goto done;
		}
		towrite -= n;
		if (towrite > 0) {
		    Dci_UpdateIov(iov, ioc, n);
		}
	    }

    	    failed = "read";
	    Ns_DStringInit(&ds);
	    ok = DciTclRead(proxyPtr->pipe[0], &ds, 1);
	    if (ok) {
		code = DciTclImport(interp, &ds, DCI_EXPORTFMT_NPROXY);
	    }
	    Ns_DStringFree(&ds);
	    ok = 1;
done:
            if (ok && refid == poolPtr->refid) {
                /*
                 * Return to front of list still running.
                 */

                Ns_MutexLock(&lock);
                proxyPtr->nextPtr = poolPtr->firstPtr;
                poolPtr->firstPtr = proxyPtr;
            } else {
                if (!ok) {
                    Tcl_AppendResult(interp, failed, " failed: ", Tcl_PosixError(interp), NULL);
                    Ns_Log(Error, "proxy[%s]:  %s", poolPtr->name, interp->result);
                }

                /*
                 * Stop this proxy.
                 */
                StopProxy(poolPtr, proxyPtr);

                Ns_MutexLock(&lock);
                if (refid == poolPtr->refid) {
                    /*
                     * return to end of list.
                     */

                    nextPtrPtr = &poolPtr->firstPtr;
                    while (*nextPtrPtr != NULL) {
                        nextPtrPtr = &((*nextPtrPtr)->nextPtr);
                    }
                    *nextPtrPtr = proxyPtr;
                    proxyPtr->nextPtr = NULL;
                } else {
                    /*
                     * If ref id changes, the pool was reinitialized and we need
                     * to toss this proxy.
                     */
                    ns_free(proxyPtr);
                }
            }
            Ns_CondSignal(&cond);
        }
    }
    Ns_MutexUnlock(&lock);
    return code;
}


static void
StopProxy(Pool *poolPtr, Proxy *proxyPtr)
{
    int status;

    if (proxyPtr->pid != -1) {
    	close(proxyPtr->pipe[0]);
    	close(proxyPtr->pipe[1]);
    	if (Ns_WaitForProcess(proxyPtr->pid, &status) != NS_OK) {
	    Ns_Log(Error, "proxy[%s]: waitpid(%d) failed: %s",
	        poolPtr->name, proxyPtr->pid, strerror(errno));
    	} else if (status != 0) {
	    Ns_Log(Error, "proxy[%s]: process %d exited with non-zero status: %d",
	    	poolPtr->name, proxyPtr->pid, status);
    	}
    	proxyPtr->pipe[0] = proxyPtr->pipe[1] = proxyPtr->pid = -1;
    }
}


static int
StartProxy(Pool *poolPtr, Proxy *proxyPtr)
{
    int ip[2], op[2], pid, n;
    char *init, *argv[6], active[100];

    n = sizeof(active) - 1;
    active[n] = '\0';
    while (--n >= 0) {
	active[n] = ' ';
    }
    init = poolPtr->init ? poolPtr->init : "";
    Ns_Log(Notice, "proxy[%s]: starting %s %s", poolPtr->name, dcitcl, init);
    argv[0] = dcitcl;
    argv[1] = "-P";
    argv[2] = poolPtr->name;
    argv[3] = init;
    argv[4] = active;
    argv[5] = NULL;
    if (ns_pipe(ip) != 0) {
badpipe:
    	Ns_Log(Error, "proxy[%s]: pipe() failed: ",
	    poolPtr->name, strerror(errno));
    	return 0;
    }
    if (ns_pipe(op) != 0) {
	close(ip[0]);
	close(ip[1]);
	goto badpipe;
    }
    pid = Ns_ExecArgv(argv[0], NULL, op[0], ip[1], argv, NULL);
    close(op[0]);
    close(ip[1]);
    if (pid < 0) {
    	Ns_Log(Error, "proxy[%s]: exec() failed: ",
	    poolPtr->name, strerror(errno));
	close(ip[0]);
	close(op[1]);
    	return 0;
    }
    proxyPtr->pipe[0] = ip[0];
    proxyPtr->pipe[1] = op[1];
    proxyPtr->pid = pid;
    return 1;
}
