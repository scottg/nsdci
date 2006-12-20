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

typedef struct {
    int fd;
    Tcl_Encoding enc;
    int refcnt;
    unsigned int nused;
    time_t otime;
    char name[4];
} File;

static File *NewFile(char *name, int fd, Tcl_Encoding enc);
static void CloseFile(void *arg);
static Tcl_CmdProc FileGetCmd;
static Tcl_CmdProc StatsCmd;
static Tcl_CmdProc AppendCmd;
static Ns_TclInterpInitProc AddCmds;

static int once = 0;
static size_t maxfds = 30;
static Ns_Cache *getCache;
static Ns_Cache *apdCache;


void
DciFileLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciFileModInit(char *server, char *module)
{
    char *path;
    int max;

    path = Ns_ConfigGetPath(server, module, "file", NULL);
    if (Ns_ConfigGetInt(path, "maxfds", &max) && max > 1) {
	maxfds = max;
    }
    Ns_Log(Notice, "file: maxfds: %d", maxfds);
    return TCL_OK;
}


int
DciFileTclInit(Tcl_Interp *interp)
{
    if (!once) {
	Ns_MasterLock();
	if (!once) {
    	    getCache = Ns_CacheCreateSz("dci:fget", TCL_STRING_KEYS, maxfds, CloseFile);
    	    apdCache = Ns_CacheCreateSz("dci:fappend", TCL_STRING_KEYS, maxfds, CloseFile);
	    once = 1;
	}
	Ns_MasterUnlock();
    }
    Tcl_CreateCommand(interp, "file.stats", StatsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "file.get", FileGetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "file.append", AppendCmd, NULL, NULL);
    return TCL_OK;
}


static int
GetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int pls)
{
    int offset, length, code;
    int fd, ok, posix_error, new;
    char *result, *fnc, *file;
    Ns_Entry *entry;
    File *fPtr;
    char *enc_name = NULL;
    Tcl_Encoding enc = NULL;
    Tcl_DString ds;
    int arg_base;

    /*
     * Accept a '-encoding' optional switch as the first argument.
     */
    arg_base = 1;
    if ((argc > 1) && STRIEQ( argv[1], "-encoding")) {
        enc_name = argv[2];
        arg_base = 3;
    }
    if (Tcl_GetInt(interp, argv[arg_base + 1], &offset) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[arg_base + 2], &length) != TCL_OK) {
    	return TCL_ERROR;
    }

    file = argv[arg_base];
    ok = 0;
    posix_error = 0;
    result = ns_malloc((size_t)(length+1));
    result[length] = '\0';
    fPtr = NULL;
    Ns_CacheLock(getCache);
    entry = Ns_CacheCreateEntry(getCache, file, &new);
    if (!new) {
        fPtr = Ns_CacheGetValue(entry);
        enc = fPtr->enc;
        fnc = NULL;
        /*
         * Handle boundry conditions: if same file accessed at one time
         * without encoding specified, and later with; vis-versa.
         * This should be very infrequent.  However, need to have code
         * behave consistently if it does happen.
         */
        if ((enc_name == NULL) != (enc == NULL)) {
            if (enc_name == NULL) {
                enc = NULL;
            } else if ((enc = Tcl_GetEncoding(NULL, enc_name)) != NULL) {
                fPtr->enc = enc; /* save specified enc */
            } else {
                fPtr = NULL; /* to get to error reporting path */
                fnc = "Tcl_GetEncoding";
            }
        }
    } else {
	fnc = "open";
	fd = open(file, O_RDONLY);
	if (fd < 0) {
            Ns_CacheDeleteEntry(entry);
            posix_error = 1;
	} else if ((enc_name != NULL) &&
                ((enc = Tcl_GetEncoding(NULL, enc_name)) == NULL)) {
            /*
             * If a encoding was specified, it needs to be a known encoding.
             */
            fnc = "Tcl_GetEncoding";
            Ns_CacheDeleteEntry(entry);
            close(fd); /* cleanup */
        } else {
	    fPtr = NewFile(file, fd, enc);
	    Ns_CacheSetValueSz(entry, fPtr, 1);
	}
    }
    if (fPtr != NULL) {
	++fPtr->nused;
	++fPtr->refcnt;

    	Ns_CacheUnlock(getCache);

	fnc = "pread";
	if (pread(fPtr->fd, result, (size_t)length, offset) == length) {
	    ok = 1;
            /*
             * If an encoding was specified, do character encoding on
             * the string obtained from the file.
             */
            if (enc != NULL) {
                (void)Tcl_ExternalToUtfDString(enc, result, -1, &ds);
                ns_free(result);
                result = Ns_DStringExport(&ds);
            }
	} else {
            posix_error = 1;
        }
    	Ns_CacheLock(getCache);
	CloseFile(fPtr);
    }
    Ns_CacheUnlock(getCache);
    if (!ok) {
        Tcl_AppendResult(interp, "could not ", fnc, " \"", file,
                         "\": ", posix_error ? Tcl_PosixError(interp) : "",
                         " encoding: ", enc_name ? enc_name : "null", NULL);
        code = TCL_ERROR;
    } else {
        Tcl_SetResult(interp, result, (Tcl_FreeProc *) ns_free);
        result = NULL;
        code = TCL_OK;
    }

    if (result != NULL) {
	ns_free(result);
    }
    return code;
}


static int
FileGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return GetCmd(arg, interp, argc, argv, 0);
}


static int
AppendCmd(ClientData arg,Tcl_Interp *interp, int argc, char **argv)
{
    int code, fd, new;
    size_t len;
    Ns_Entry *entry;
    File *fPtr;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " file string\"", NULL);
	return TCL_ERROR;
    }

    len = strlen(argv[2]);
    code = TCL_ERROR;
    Ns_CacheLock(apdCache);
    entry = Ns_CacheCreateEntry(apdCache, argv[1], &new);
    if (!new) {
	fPtr = Ns_CacheGetValue(entry);
    } else {
	fd = open(argv[1], O_WRONLY|O_APPEND|O_CREAT, 0644);
	if (fd < 0) {
            Tcl_AppendResult(interp, "open (\"", argv[1], "\") failed: ",
		Tcl_PosixError(interp), NULL);
	    Ns_CacheDeleteEntry(entry);
	    goto unlock;
	}
	fPtr = NewFile(argv[1], fd, NULL);
	Ns_CacheSetValueSz(entry, fPtr, 1);
    }
    ++fPtr->nused;
    if (write(fPtr->fd, argv[2], len) != len) {
	Tcl_AppendResult(interp, "write (\"", argv[1], "\") failed: ",
	    Tcl_PosixError(interp), NULL);
	goto unlock;
    }
    code = TCL_OK;
unlock:
    Ns_CacheUnlock(apdCache);
    return code;
}


static int
StatsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cache;
    Ns_Entry *ent;
    Ns_CacheSearch search;
    Tcl_DString ds;
    File *fPtr;
    char buf[100];

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " ?cache?\"", NULL);
	return TCL_ERROR;
    }
    cache = getCache;
    if (argc == 2) {
	if (STREQ(argv[1], "append")) {
	    cache = apdCache;
	} else if (!STREQ(argv[1], "file")) {
	    Tcl_AppendResult(interp, "no such cache: \"", argv[1],
		"\": should be append, file, or mmap", NULL);
	    return TCL_ERROR;
	}
    }

    Tcl_DStringInit(&ds);
    Ns_CacheLock(cache);
    ent = Ns_CacheFirstEntry(cache, &search);
    while (ent != NULL) {
	fPtr = Ns_CacheGetValue(ent);
	Tcl_DStringStartSublist(&ds);
	Tcl_DStringAppendElement(&ds, "name");
	Tcl_DStringAppendElement(&ds, fPtr->name);
	sprintf(buf, " nused %d otime %d", fPtr->nused, (int) fPtr->otime);
	Tcl_DStringAppend(&ds, buf, -1);
	Tcl_DStringEndSublist(&ds);
	ent = Ns_CacheNextEntry(&search);
    }
    Ns_CacheUnlock(cache);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}


static void
CloseFile(void *arg)
{
    File *fPtr = arg;

    if (--fPtr->refcnt == 0) {
    	close(fPtr->fd);
        if (fPtr->enc != NULL) {
            Tcl_FreeEncoding(fPtr->enc);
        }
    	Ns_Log(Notice, "file: closed: %s", fPtr->name);
    	ns_free(fPtr);
    }
}


static File *
NewFile(char *name, int fd, Tcl_Encoding enc)
{
    File *fPtr;

    if (fd >= 0) {
	Ns_CloseOnExec(fd);
    }
    fPtr = ns_malloc(sizeof(File) + strlen(name));
    strcpy(fPtr->name, name);
    fPtr->fd = fd;
    fPtr->enc = enc;
    fPtr->refcnt = 1;
    fPtr->nused = 0;
    fPtr->otime = time(NULL);
    Ns_Log(Notice, "file: opened: %s", name);
    return fPtr;
}
