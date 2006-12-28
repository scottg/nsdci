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

#define QF_LEN 15
#define QF_DOT 11
#define QF_TEMPLATE "qXXXXXXXXXX.XXX"
#define QF_FORMAT "q%.10d.%.3d"
#define QF_FILE_OK(file) (file[0] == 'q' && strlen(file) == QF_LEN && file[QF_DOT] == '.')
#define QF_MAXWAIT 3600

static Ns_Callback QueStop;
static Ns_Callback QueStart;
static Ns_ThreadProc QueThread;

typedef struct Que {
    char *name;
    char *server;
    char *script;
    time_t nextRun;
    Ns_Mutex lock;
    Ns_Cond cond;
} Que;

static char *qfDir = NULL;

int
DciQueFileInit(char *server, char *module)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, dciDir, "queue", NULL);
    qfDir = Ns_DStringExport(&ds);
    return NS_OK;
}

static int
FileSort(const void *e1, const void *e2)
{
    char **s1, **s2;

    s1 = (char **) e1;
    s2 = (char **) e2;
    return strcmp(*s1, *s2);
}

int
Dci_QfGetAllCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int start, end, current;
    DIR *dp;
    struct dirent *ent;
    char **files, *file;
    int nfiles, maxfiles;
    Ns_DString dir;

    if (argc < 2 || argc >  4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " qfName ?start end?\"", NULL);
	return TCL_ERROR;
    }
    if (argc < 3) {
	start = 0;
    } else if (Tcl_GetInt(interp, argv[2], &start) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc < 4) {
	end = INT_MAX;
    } else if (Tcl_GetInt(interp, argv[3], &end) != TCL_OK) {
	return TCL_ERROR;
    }

    Ns_DStringInit(&dir);
    dp = opendir(Ns_MakePath(&dir, qfDir, argv[1], NULL));
    Ns_DStringFree(&dir);
    if (dp == NULL) {
	Tcl_AppendResult(interp, "opendir(\"", argv[1],
	    "\") failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }

    nfiles = 0;
    maxfiles = 100;
    files = ns_malloc(maxfiles * sizeof(char *));
    while ((ent = ns_readdir(dp)) != NULL) {
	file = ent->d_name;
	if (QF_FILE_OK(file)) {
	    current = strtol(file+1, NULL, 10);
	    if (current >= start && current <= end) {
		if (nfiles == maxfiles) {
		    maxfiles *= 2;
		    files = ns_realloc(files, maxfiles * sizeof(char *));
		}
		files[nfiles] = ns_strdup(file);
		++nfiles;
	    }
	}
    }
    closedir(dp);

    if (nfiles > 0) {
	Tcl_DString ds;
	int i;

	Tcl_DStringInit(&ds);
	qsort(files, nfiles, sizeof(char *), FileSort);
	for (i = 0; i < nfiles; ++i) {
	    Tcl_DStringAppendElement(&ds, files[i]);
	    ns_free(files[i]);
	}
	Tcl_DStringResult(interp, &ds);
    }

    ns_free(files);
    return TCL_OK;
}

int
Dci_QfGetFirstCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    DIR *dp;
    struct dirent *ent;
    char *file;
    long current;
    long first = INT_MAX;
    long nfiles = 0;
    Ns_DString dir;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " qfName\"", NULL);
	return TCL_ERROR;
    }

    Ns_DStringInit(&dir);
    dp = opendir(Ns_MakePath(&dir, qfDir, argv[1], NULL));
    Ns_DStringFree(&dir);
    if (dp == NULL) {
	Tcl_AppendResult(interp, "opendir(\"", argv[1],
	    "\") failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }

    while ((ent = ns_readdir(dp)) != NULL) {
	file = ent->d_name;
	if (QF_FILE_OK(file)) {
	    ++nfiles;
	    current = strtol(file+1, NULL, 10);
	    if (current < first) {
		first = current;
	    }
	}
    }
    closedir(dp);

    sprintf(interp->result, "%ld", nfiles ? first : -1);
    return TCL_OK;
}


int
Dci_QfGetDirCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString dir;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " qfName\"", NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&dir);
    Ns_MakePath(&dir, qfDir, argv[1], NULL);
    Tcl_SetResult(interp, dir.string, TCL_VOLATILE);
    Ns_DStringFree(&dir);
    return TCL_OK;
}


int
Dci_QfInsertCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString path;
    int major, minor;
    int code = TCL_ERROR;
    char *file;

    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " qfName tFile major\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &major) != TCL_OK) {
	return TCL_ERROR;
    }

    Ns_DStringInit(&path);
    Ns_MakePath(&path, qfDir, argv[1], QF_TEMPLATE, NULL);
    file = &path.string[path.length - QF_LEN];
    minor = 1;
    while (1) {
	sprintf(file, QF_FORMAT, major, minor);
	if (link(argv[2], path.string) == 0) {
	    unlink(argv[2]);
	    Tcl_SetResult(interp, file, TCL_VOLATILE);
	    code = TCL_OK;
	    break;
	}
	if (errno != EEXIST) {
	    Tcl_AppendResult(interp, "link(\"", argv[2], ", ",
		path.string, "\") failed: ", Tcl_PosixError(interp), NULL);
	    goto done;
	}
	++minor;
    }
done:
    Ns_DStringFree(&path);
    return code;
}


Dci_Que *
DciQueCreate(char *server, char *name, char *script)
{
    Que *qPtr;
    
    qPtr = ns_malloc(sizeof(Que));
    Ns_MutexInit(&qPtr->lock);
    Ns_CondInit(&qPtr->cond);
    qPtr->nextRun = time(NULL);
    qPtr->server = server;
    qPtr->name = name;
    qPtr->script = script;
    Ns_RegisterServerShutdown(server, QueStop, qPtr);
    Ns_RegisterAtStartup(QueStart, qPtr);
    return (Dci_Que *) qPtr;
}


void
DciQueNext(Dci_Que *que, time_t next)
{
    Que *qPtr = (Que *) que;

    Ns_MutexLock(&qPtr->lock);
    if (qPtr->nextRun < 0 || qPtr->nextRun > next) {
    	qPtr->nextRun = next;
	Ns_CondSignal(&qPtr->cond);
    }
    Ns_MutexUnlock(&qPtr->lock);
}
       
    
static void
QueStop(void *arg)
{
    Que *qPtr = (Que *) arg;

    Ns_MutexLock(&qPtr->lock);
    Ns_CondSignal(&qPtr->cond);
    Ns_MutexUnlock(&qPtr->lock);
}

static void
QueStart(void *arg)
{
    Ns_ThreadCreate(QueThread, arg, 0, NULL);
}


static void
QueThread(void *arg)
{
    Que *qPtr = (Que *) arg;
    Tcl_Interp *interp;
    Ns_Time timeout;
    int wait;

    Ns_ThreadSetName(qPtr->name);
    Ns_Log(Notice, "starting");

    while (!Ns_InfoShutdownPending()) {
	interp = Ns_TclAllocateInterp(qPtr->server);
	if (Tcl_GlobalEval(interp, qPtr->script) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
        Ns_TclDeAllocateInterp(interp);
	Ns_MutexLock(&qPtr->lock);
	if (qPtr->nextRun < 0) {
	    wait = QF_MAXWAIT;
	} else {
    	    wait = (int) difftime(qPtr->nextRun, time(NULL));
	}
	if (wait > 0) {
    	    Ns_GetTime(&timeout);
	    Ns_IncrTime(&timeout, wait > QF_MAXWAIT ? QF_MAXWAIT : wait, 0);
    	    Ns_CondTimedWait(&qPtr->cond, &qPtr->lock, &timeout);
	}
	qPtr->nextRun = time(NULL) + QF_MAXWAIT;
	Ns_MutexUnlock(&qPtr->lock);
    }
}
