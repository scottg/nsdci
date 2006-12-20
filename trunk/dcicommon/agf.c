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

typedef struct AgfKey {
    dev_t dev;
    ino_t ino;
} AgfKey;

typedef struct AgfData {
    int refCount;
    off_t size;
    time_t mtime;
    Dci_List list;
} AgfData;

#define Cnt(dp)		((dp)->list.nelem)

static int AddCmds(Tcl_Interp *interp, void *ignored);
static int AgfGet(Tcl_Interp *interp, char *file, AgfData **agfPtr);
static int AgfDecr(AgfData *dPtr);
static void AgfRelease(AgfData *dPtr, char *file);
static Ns_Callback AgfFree;
static AgfData *AgfRead(Tcl_Interp *interp, char *file, struct stat *stPtr);
static int CmpElem(const void *e1, const void *e2);
static Tcl_CmdProc AgfGetCmd;
static Tcl_CmdProc AgfCopyCmd;
static Tcl_CmdProc AgfNamesCmd;
static int cachesize;
static int maxentry;
static int debug;
static Ns_Cache *cache;


void
DciAgfLibInit(void)
{
    DciAddIdent(rcsid);
    maxentry = 2 * 1024 * 1000;
    cachesize = maxentry * 10;
    debug = 0;
}


int
DciAgfModInit(char *server, char *module)
{
    int n;
    char *path;

    path = Ns_ConfigGetPath(server, module, "agf", NULL);
    if (Ns_ConfigGetInt(path, "cachesize", &n) && n > 0) {
	cachesize = n;
    }
    if (Ns_ConfigGetInt(path, "maxentry", &n) && n > 0) {
	maxentry = n;
	if (maxentry > cachesize / 2) {
	    maxentry = cachesize / 2;
	}
    }
    debug = 0;
    return NS_OK;
}


int
DciAgfTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "agf.debug", DciSetDebugCmd, &debug, NULL);
    Tcl_CreateCommand(interp, "agf.get", AgfGetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "agf.arraySet", AgfCopyCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "agf.copy", AgfCopyCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "agf.names", AgfNamesCmd, NULL, NULL);
    return NS_OK;
}


static int
AgfGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Elem *elemPtr;
    AgfData *dPtr = NULL;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " file varName\"", NULL);
        return TCL_ERROR;
    }
    if (AgfGet(interp, argv[1], &dPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    elemPtr = Dci_ListSearch(&dPtr->list, argv[2]);
    if (elemPtr != NULL) {
        Tcl_SetResult(interp, elemPtr->value, TCL_VOLATILE);
    }
    AgfRelease(dPtr, argv[1]);
    return TCL_OK;
}


static int
AgfNamesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    AgfData *dPtr = NULL;
    char *key;
    int i;
    
    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " file ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (AgfGet(interp, argv[1], &dPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < Cnt(dPtr); ++i) {
	key = Dci_ListKey(&dPtr->list, i);
	if (argc == 2 || Tcl_StringMatch(key, argv[2])) {
            Tcl_AppendElement(interp, key);
	}
    }
    AgfRelease(dPtr, argv[1]);
    return TCL_OK;
}


static int
AgfCopyCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    AgfData *dPtr;
    char *key;
    int i, status = TCL_OK;
    
    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " file arrayName ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (AgfGet(interp, argv[1], &dPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < Cnt(dPtr); ++i) {
	key = Dci_ListKey(&dPtr->list, i);
	if (argc == 3 || Tcl_StringMatch(key, argv[3])) {
            if (Tcl_SetVar2(interp, argv[2], key, Dci_ListValue(&dPtr->list, i),
			    TCL_LEAVE_ERR_MSG) == NULL) {
                status = TCL_ERROR;
                break;
            }
        }
    }
    AgfRelease(dPtr, argv[1]);
    return status;
}


static int
AgfGet(Tcl_Interp *interp, char *file, AgfData **agfPtr)
{
    struct stat st;
    AgfData *dPtr;
    Ns_Entry *ePtr;
    AgfKey key;
    int new;
    
    if (stat(file, &st) != 0) {
        Tcl_AppendResult(interp, "could not stat \"",
            file, "\": ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    if (S_ISREG(st.st_mode) == 0) {
        Tcl_AppendResult(interp, "not an ordinary file: ", file, NULL);
        return TCL_ERROR;
    }

    /*
     * Create the cache the first time.
     */

    if (cache == NULL) {
	Ns_MasterLock();
	if (cache == NULL) {
    	    cache = Ns_CacheCreateSz("dci:agf", sizeof(AgfKey)/sizeof(int),
                                     (size_t)cachesize, AgfFree);
	}
	Ns_MasterUnlock();
    }

    /*
     * Skip cache for large files.
     */

    if (st.st_size > maxentry) {
	dPtr = AgfRead(interp, file, &st);
	goto done;
    }

    /*
     * Read through cache for small files.
     */

    key.dev = st.st_dev;
    key.ino = st.st_ino;
    dPtr = NULL;
    Ns_CacheLock(cache);
    ePtr = Ns_CacheCreateEntry(cache, (char *) &key, &new);
    if (!new) {
        while (ePtr != NULL && (dPtr = Ns_CacheGetValue(ePtr)) == NULL) {
            Ns_CacheWait(cache);
            ePtr = Ns_CacheFindEntry(cache, (char *) &key);
        }
        if (dPtr == NULL) {
            Tcl_AppendResult(interp, "wait failed for file: ", file, NULL);
        } else if (dPtr->mtime != st.st_mtime || dPtr->size != st.st_size) {
            Ns_CacheUnsetValue(ePtr);
	    dPtr = NULL;
            new = 1;
        }
    }
    if (new) {
        Ns_CacheUnlock(cache);
	dPtr = AgfRead(interp, file, &st);
        Ns_CacheLock(cache);
	ePtr = Ns_CacheCreateEntry(cache, (char *) &key, &new);
        if (dPtr == NULL) {
            Ns_CacheFlushEntry(ePtr);
        } else {
            Ns_CacheSetValueSz(ePtr, dPtr, (size_t)dPtr->size);
        }
        Ns_CacheBroadcast(cache);
    }
    if (dPtr != NULL) {
	++dPtr->refCount;
    }
    Ns_CacheUnlock(cache);

done:
    if (dPtr == NULL) {
	return TCL_ERROR;
    }
    *agfPtr = dPtr;
    return TCL_OK;
}


static AgfData *
AgfRead(Tcl_Interp *interp, char *file, struct stat *stPtr)
{
    AgfData *dPtr;
    Dci_List list;
    char *buf;
    int fd, nread;

    dPtr = NULL;
    fd = open(file, O_RDONLY);
    if (fd < 0) {
        Tcl_AppendResult(interp, "open failed: ", file, "\"", 
            Tcl_PosixError(interp), "\"", NULL);
    } else {            
        buf = ns_malloc((size_t)(stPtr->st_size+1));
        buf[stPtr->st_size] = '\0';
	nread = read(fd, buf, (size_t)stPtr->st_size);
        close(fd);
	if (nread < stPtr->st_size) {
            Tcl_AppendResult(interp, "could not read \"", file, "\": ",
			     Tcl_PosixError(interp), NULL);
	} else if (Dci_ListInit(&list, buf) != TCL_OK) {
            Tcl_AppendResult(interp, "invalid agf: ", file, NULL);
        } else {
            dPtr = ns_calloc(1, sizeof(*dPtr));
            dPtr->refCount = 1;
            dPtr->mtime = stPtr->st_mtime;
            dPtr->size = stPtr->st_size;
	    dPtr->list = list;
        }
        ns_free(buf);
    }
    if (debug) {
	if (dPtr != NULL) {
	    Ns_Log(Notice, "agf: read %s = %p", file, dPtr);
	} else {
	    Ns_Log(Error, "agf: read %s failed: %s", file, strerror(errno));
	}
    }
    return dPtr;
}


static void
AgfRelease(AgfData *dPtr, char *file)
{
    int freed;

    Ns_CacheLock(cache);
    freed = AgfDecr(dPtr);
    Ns_CacheUnlock(cache);
    if (debug && freed) {
	Ns_Log(Notice, "agf: freed file %s", file);
    }
}


static void
AgfFree(void *arg)
{
    AgfData *dPtr = arg;

    if (AgfDecr(dPtr) && debug) {
	Ns_Log(Notice, "agf: pruned entry %p", dPtr);
    }
}


static int
AgfDecr(AgfData *dPtr)
{
    if (--dPtr->refCount > 0) {
	return 0;
    }
    Dci_ListFree(&dPtr->list);
    ns_free(dPtr);
    return 1;
}
