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

/*
 * Note on error handling:
 *
 * All requests are handled via NfsProc which returns NS_OK (0) on success and
 * either NS_ERROR (-1) or a Unix errno code (positive numbers) on failure.
 * Previous RPC client code would interpret this as RPC_OK (0) on success or 
 * simply pass a non-RPC_OK status to Dci_RpcError to format a result. 
 * Negative results were interpreted as RPC protocol errors (RPC_TIMEOUT,
 * RPC_NOCONN, or RPC_ERROR), positive results were assumed Unix errno
 * results.  Overall this was sloppy.  The new Dci_RpcRun code correctly
 * separates the RPC request status (job.error) from the server result
 * code (job.result).  
 */

#include "dci.h"

/*
 * NFS flags.
 */

#define DCI_NFSREADONLY 1
#define DCI_NFSMKDIRS   2
#define DCI_NFSRENAME	4
#define DCI_NFSSTATS	8

/*
 * NFS server structures.
 */
 
typedef struct {
    char *name;
    char  rpcname[DCI_RPCNAMESIZE];
    char *flushcache;
    char *root;
    int   flags;
    int   maxposts;
    Ns_Cache *cache;
    Ns_Mutex statslock;
    Tcl_HashTable stats;
    unsigned long nreadhit;
    unsigned long nreadmiss;
    unsigned long nreadnoent;
    unsigned long nwrite;
    unsigned long ncopy;
    unsigned long nappend;
    unsigned long nunlink;
    unsigned long ncbpost;
    unsigned long ncbdel;
    unsigned long nerror;
} Nfs;

typedef struct {
    Nfs *nfsPtr;
    unsigned long nread;/* Entry read count. */
    Ns_Time atime;	/* Entry last access time. */
    Ns_Time ctime;	/* Entry create time. */
    struct stat st;	/* Last file stat data. */
    size_t bufsize;	/* Size of data buffer. */
    char buf[2];	/* Data buffer. */
} File;

typedef struct {
    Ns_Time  rtime;              /* time of last stats reset */
    unsigned long nread;         /* number of reads */
    unsigned long nbytesread;    /* total bytes read */
    unsigned long nmaxbytesread; /* max bytes read in one request */
    unsigned long nminbytesread; /* min bytes read in one request */
    Ns_Time nsecsread;           /* total number of seconds for reads */
    Ns_Time nmaxsecsread;        /* max seconds taken for one read request */
    Ns_Time nminsecsread;        /* min seconds taken for one read request */
    unsigned long nwrite;        /* number of writes */
    unsigned long nbyteswrite;   /* total bytes written */
    unsigned long nmaxbyteswrite;/* max bytes written in one request */
    unsigned long nminbyteswrite;/* min bytes written in one request */
    Ns_Time nsecswrite;          /* total number of seconds for writes */
    Ns_Time nmaxsecswrite;       /* max seconds taken for one write request */
    Ns_Time nminsecswrite;       /* min seconds taken for one write request */
} Stats;

static Dci_RpcProc NfsProc;
static int NfsReadFile(Nfs *nfsPtr, char *file, Ns_DString *dsPtr);
static int NfsWriteFile(Nfs *nfsPtr, Ns_DString *dsPtr);
static int NfsCopyFile(Nfs *nfsPtr, Ns_DString *dsPtr);
static int NfsAppendFile(Nfs *nfsPtr, Ns_DString *dsPtr);
static int NfsWriteFile2(Nfs *nfsPtr, char *file, char *bytes, int len);
static int NfsWriteFile3(Nfs *nfsPtr, char *file, char *bytes, int len, int nl, int append);
static int NfsUnlinkFile(Nfs *nfsPtr, Ns_DString *dsPtr);
static int NfsCbUpdate(Nfs *nfsPtr, int delete, Ns_DString *dsPtr);
static char *NfsPath(Ns_DString *dsPtr, Nfs *nfsPtr, char *file);
static void NfsFlush(Nfs *nfsPtr, char *file);
static int NfsMkdirs(char *dir);
static Stats *NfsGetStatsDetail(Nfs *nfsPtr, char *name);
static void NfsResetStatsDetail(Stats *statsPtr);
static void NfsReportStatsDetail(Tcl_Interp *interp, Nfs *nfsPtr, int reset);
static int NfsGetServer(Tcl_Interp *interp, char *name, Nfs **nfsPtrPtr);

static Tcl_CmdProc NfsNamesCmd;
static Tcl_CmdProc NfsFilesCmd;
static Tcl_CmdProc NfsStatsCmd;
static Tcl_CmdProc NfsStatsDetailCmd;
static Tcl_CmdProc NfsStatsToggleCmd;
static Ns_TclInterpInitProc AddCmds;

static Tcl_HashTable nfsTable;
static int fDebug;



/*
 *----------------------------------------------------------------------
 *
 * DciNfsInit --
 *
 *      Initialize sob servers based AOLserver configuration.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Creation of sob servers.
 *
 *----------------------------------------------------------------------
 */

int
DciNfsInit(char *server, char *module)
{
    Tcl_HashEntry *hPtr;
    char *path, *name, *handshake;
    char handshakeName[DCI_RPCNAMESIZE];
    Ns_Set *set, *clients;
    int i, opt, flush, cachesize, new;
    Nfs *nfsPtr;
    
    Dci_LogIdent(module, rcsid);

    /*
     * Initialize the NFS servers and SOB clients tables and
     * core options.
     */
     
    Tcl_InitHashTable(&nfsTable, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "nfs", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
	fDebug = 0;
    }
    
    /* 
     * Create NFS servers.
     */
    
    path = Ns_ConfigGetPath(server, module, "sob/servers", NULL);
    set = Ns_ConfigGetSection(path);
    hPtr = NULL;
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	name = Ns_SetKey(set, i);
    	path = Ns_ConfigGetPath(server, module, "sob/server", name, "clients", NULL);
	clients = Ns_ConfigGetSection(path);
	if (clients == NULL) {
	    Ns_Log(Error, "nfs[%s]: missing clients section", name);
	    return NS_ERROR;
	}
	hPtr = Tcl_CreateHashEntry(&nfsTable, name, &new);
	if (!new) {
	    Ns_Log(Error, "nfs: multiply defined: %s", name);
	    return NS_ERROR;
	}
	nfsPtr = ns_calloc(1, sizeof(Nfs));
	Tcl_SetHashValue(hPtr, nfsPtr);

	if (Dci_SobRpcName(name, nfsPtr->rpcname) != NS_OK) {
	    break;
	}
    	nfsPtr->name = name;
    	path = Ns_ConfigGetPath(server, module, "sob/server", name, NULL);
	nfsPtr->root = Ns_ConfigGet(path, "root");
	if (nfsPtr->root == NULL) {
	    Ns_Log(Error, "nfs[%s]: missing root", name);
	    break;
	}
	if (mkdir(nfsPtr->root, 0755) != 0 && errno != EEXIST) {
    	    Ns_Log(Error, "nfs[%s]: mkdir(%s) failed: %s", name,
	    	   nfsPtr->root, strerror(errno));
	    break;
	}
	Tcl_InitHashTable(&nfsPtr->stats, TCL_STRING_KEYS);
	handshake = Ns_ConfigGet(path, "handshake");
	if (handshake == NULL) {
	    handshake = Ns_ConfigGet(path, "handshakeName");
	}
	if (handshake != NULL) {
            if (Dci_SobRpcName(handshake, handshakeName) == NS_OK) {
                handshake = handshakeName;
            } else {
		break;
	    }
	}
	if (!Ns_ConfigGetBool(path, "maxposts", &nfsPtr->maxposts)) {
	    nfsPtr->maxposts = 500;
	}
	if (!Ns_ConfigGetBool(path, "sendflush", &flush)) {
	    flush = 1;
	}
	if (flush) {
	    nfsPtr->flushcache = Ns_ConfigGet(path, "flushcache");
	    if (nfsPtr->flushcache == NULL) {
	    	nfsPtr->flushcache = nfsPtr->rpcname;
	    }
	}
	if (Ns_ConfigGetBool(path, "readonly", &opt) && opt) {
	    nfsPtr->flags |= DCI_NFSREADONLY;
	}
	if (Ns_ConfigGetBool(path, "mkdirs", &opt) && opt) {
	    nfsPtr->flags |= DCI_NFSMKDIRS;
	}
	if (!Ns_ConfigGetBool(path, "rename", &opt) || opt) {
	    nfsPtr->flags |= DCI_NFSRENAME;
	}
	if (!Ns_ConfigGetInt(path, "cachesize", &cachesize)) {
	    cachesize = 5*1024*1024;
	}
	if (Ns_ConfigGetBool(path, "statsdetail", &opt) && opt) {
	    nfsPtr->flags |= DCI_NFSSTATS;
	}
        Ns_MutexInit(&nfsPtr->statslock);
	Ns_MutexSetName2(&nfsPtr->statslock, "nfs:stats", name);
	if (Dci_RpcCreateServer(server, module, nfsPtr->rpcname, handshake,
				clients, NfsProc, nfsPtr) != NS_OK) {
	    break;
	}
	nfsPtr->cache = Ns_CacheCreateSz(name, TCL_STRING_KEYS,
					 (size_t) cachesize, ns_free);
	Ns_Log(Notice, "nfs[%s]: serving %s%s", name, nfsPtr->root,
		(nfsPtr->flags & DCI_NFSREADONLY) ? " (readonly)" : "");
	hPtr = NULL;
    }

    /*
     * On error, free the errant nfsPtr and return NS_ERROR;
     */

    if (hPtr != NULL) {
	Tcl_DeleteHashEntry(hPtr);
	ns_free(nfsPtr);
	return TCL_ERROR;
    }
	    
    /*
     * Add Tcl commands.
     */

    Ns_TclInitInterps(server, AddCmds, NULL);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AddCmds --
 *
 *      Ns_TclInitInterps() callback to add Tcl commands.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Tcl commands are added to the master interpreter procedure 
 *      table.
 *
 *----------------------------------------------------------------------
 */

static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    /*
     * Add the basic NFS commands.
     */
     
    Tcl_CreateCommand(interp, "nfs.names", NfsNamesCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nfs.files", NfsFilesCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nfs.stats", NfsStatsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nfs.statsd", NfsStatsDetailCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nfs.collect", NfsStatsToggleCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "nfs.debug", DciSetDebugCmd, &fDebug, NULL);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsNamesCmd --
 *
 *      Tcl interface for the nfs.names commands.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsNamesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *pattern, *key;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?pattern?\n", NULL);
	return TCL_ERROR;
    }
    pattern = argv[1];
    hPtr = Tcl_FirstHashEntry(&nfsTable, &search);
    while (hPtr != NULL) {
	key = Tcl_GetHashKey(&nfsTable, hPtr);
	if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
	    Tcl_AppendElement(interp, key);
	}
    	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsFilesCmd --
 *
 *      Implement the nfs.files command to show cached files.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsFilesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashSearch search;
    Tcl_DString ds;
    Ns_Entry *entry;
    char *pattern, *key, buf[100];
    Nfs *nfsPtr;
    File *fPtr;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server ?pattern?\"", NULL);
	return TCL_ERROR;
    }
    if (NfsGetServer(interp, argv[1], &nfsPtr) != TCL_OK) {	
	return TCL_ERROR;
    }
    pattern = argv[2];

    Tcl_DStringInit(&ds);
    Ns_CacheLock(nfsPtr->cache);
    entry = Ns_CacheFirstEntry(nfsPtr->cache, &search);
    while (entry != NULL) {
	key = Ns_CacheKey(entry);
	fPtr = Ns_CacheGetValue(entry);
	if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
	    Tcl_DStringStartSublist(&ds);
	    Tcl_DStringAppendElement(&ds, key);
	    sprintf(buf, " %ld %ld %ld %ld %ld",
		fPtr->nread, fPtr->bufsize, fPtr->st.st_mtime,
		fPtr->ctime.sec, fPtr->atime.sec);
	    Tcl_DStringAppend(&ds, buf, -1);
	    Tcl_DStringEndSublist(&ds);
	}
    	entry = Ns_CacheNextEntry(&search);
    }
    Ns_CacheUnlock(nfsPtr->cache);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsStatsToggleCmd --
 *
 *      Toggle detailed statistic collection for a given server.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsStatsToggleCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Nfs *nfsPtr;
    int stats;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server 1|0\"", NULL);
	return TCL_ERROR;
    }
    if (NfsGetServer(interp, argv[1], &nfsPtr) != TCL_OK) {	
	return TCL_ERROR;
    }
    if (Tcl_GetBoolean(interp, argv[2], &stats) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&nfsPtr->statslock);
    if (stats) {
	nfsPtr->flags |= DCI_NFSSTATS;
    } else {
	nfsPtr->flags &= ~DCI_NFSSTATS;
        hPtr = Tcl_FirstHashEntry(&nfsPtr->stats, &search);
        while (hPtr != NULL) {
            ns_free(Tcl_GetHashValue(hPtr));
            hPtr = Tcl_NextHashEntry(&search);
        } 
        Tcl_DeleteHashTable(&nfsPtr->stats);
        Tcl_InitHashTable(&nfsPtr->stats, TCL_STRING_KEYS);
    }
    Ns_MutexUnlock(&nfsPtr->statslock); 
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsStatsCmd --
 *
 *      Implement the nfs.stats command to return current counters.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
AddStat(Tcl_Interp *interp, char *var, char *name, unsigned long value)
{
    char buf[20];

    sprintf(buf, "%lu", value);
    if (Tcl_SetVar2(interp, var, name, buf, TCL_LEAVE_ERR_MSG) == NULL) {
	return 0;
    } 
    return 1;
}


static int
NfsStatsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Nfs *nfsPtr;
    int status;
    char *var;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server varName\"", NULL);
	return TCL_ERROR;
    }
    if (NfsGetServer(interp, argv[1], &nfsPtr) != TCL_OK) {	
	return TCL_ERROR;
    }
    var = argv[2];
    status = TCL_ERROR;
    do {
    	if (!AddStat(interp, var, "nreadhit", nfsPtr->nreadhit)) break;
    	if (!AddStat(interp, var, "nreadmiss", nfsPtr->nreadmiss)) break;
    	if (!AddStat(interp, var, "nreadnoent", nfsPtr->nreadnoent)) break;
    	if (!AddStat(interp, var, "nwrite", nfsPtr->nwrite)) break;
    	if (!AddStat(interp, var, "ncopy", nfsPtr->ncopy)) break;
    	if (!AddStat(interp, var, "nappend", nfsPtr->nappend)) break;
    	if (!AddStat(interp, var, "nunlink", nfsPtr->nunlink)) break;
    	if (!AddStat(interp, var, "ncbpost", nfsPtr->ncbpost)) break;
    	if (!AddStat(interp, var, "ncbdel", nfsPtr->ncbdel)) break;
    	if (!AddStat(interp, var, "nerror", nfsPtr->nerror)) break;
	status = TCL_OK;
    } while (0);
    return status;

}


/*
 *----------------------------------------------------------------------
 *
 * NfsStatsDetailCmd --
 *
 *      Implement the nfs.statsd command to return detailed level stats
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsStatsDetailCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Nfs *nfsPtr;
    int reset;

    if (argc != 2 && argc != 3) {
usage:
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?-reset? server\"", NULL);
	return TCL_ERROR;
    }  
    if (argc == 2) {
	reset = 0;
    } else {
	if (!STREQ(argv[1],"-reset")) {
	    goto usage;
	}
	reset = 1;
    }
    if (NfsGetServer(interp, argv[argc-1], &nfsPtr) != TCL_OK) {	
	return TCL_ERROR;
    }   
    Ns_MutexLock(&nfsPtr->statslock);
    NfsReportStatsDetail(interp, nfsPtr, reset);
    Ns_MutexUnlock(&nfsPtr->statslock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsReportDetail --
 *
 *      Format detail statistics for tcl proc result
 *
 * Results:
 *      Void
 *
 * Side effects:
 *      Detailed statistics stored in the interp result string
 *      If reset is non-zero, after statistics are retrieved, they
 *      are set to 0.
 *
 *      Calling function must ensure that proper mutex locking occurs.
 *
 *----------------------------------------------------------------------
 */

static void
NfsReportStatsDetail(Tcl_Interp *interp, Nfs *nfsPtr, int reset)
{
    Stats *statsPtr = NULL;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *name;
    char buf[1024];
    Tcl_DString results;
    Ns_Time avgread, avgwrite;
    unsigned long avgbytesread, avgbyteswrite;
    Ns_Time now;

    Tcl_DStringInit(&results);
    if (nfsPtr->flags & DCI_NFSSTATS) {
        hPtr = Tcl_FirstHashEntry(&nfsPtr->stats, &search);
        while (hPtr != NULL) {
            name = Tcl_GetHashKey(&nfsPtr->stats, hPtr);
            statsPtr = Tcl_GetHashValue(hPtr);
            Tcl_DStringStartSublist(&results);
            Tcl_DStringAppendElement(&results, name);
            if (statsPtr->nread > 0) {
                avgread.sec = statsPtr->nsecsread.sec/statsPtr->nread;      
                avgread.usec = statsPtr->nsecsread.usec/statsPtr->nread;
                Ns_AdjTime(&avgread);
                avgbytesread = statsPtr->nbytesread/statsPtr->nread;
            } else {
                avgread.sec = 0;
                avgread.usec = 0;
                avgbytesread = 0;
            }
            if (statsPtr->nwrite > 0) {
                avgwrite.sec = statsPtr->nsecswrite.sec/statsPtr->nwrite;
                avgwrite.usec = statsPtr->nsecswrite.usec/statsPtr->nwrite;
                Ns_AdjTime(&avgwrite);
                avgbyteswrite = statsPtr->nbyteswrite/statsPtr->nwrite;
            } else {
                avgwrite.sec = 0;
                avgwrite.usec = 0;
                avgbyteswrite = 0;
            }
            Ns_GetTime(&now);
            sprintf(buf, "%lu %lu %lu %lu %lu %lu %lu.%06ld "
			 "%lu.%06ld %lu.%06ld %lu %lu %lu %lu "
			 "%lu.%06ld %lu.%06ld %lu.%06ld",
                    (unsigned long) statsPtr->rtime.sec,
                    (unsigned long) now.sec,
                    statsPtr->nread,
                    avgbytesread,
                    statsPtr->nmaxbytesread,
                    statsPtr->nminbytesread,
                    avgread.sec,
                    avgread.usec,
                    statsPtr->nmaxsecsread.sec,
                    statsPtr->nmaxsecsread.usec,
                    statsPtr->nminsecsread.sec,
                    statsPtr->nminsecsread.usec,
                    statsPtr->nwrite,
                    avgbyteswrite,
                    statsPtr->nmaxbyteswrite,
                    statsPtr->nminbyteswrite,
                    avgwrite.sec,
                    avgwrite.usec,
                    statsPtr->nmaxsecswrite.sec,
                    statsPtr->nmaxsecswrite.usec,
                    statsPtr->nminsecswrite.sec,
                    statsPtr->nminsecswrite.usec);
            Tcl_DStringAppendElement(&results, buf);
            Tcl_DStringEndSublist(&results);
            if (reset) {
                NfsResetStatsDetail(statsPtr);
            }
            hPtr = Tcl_NextHashEntry(&search);
        } 
    }
    Tcl_DStringResult(interp, &results);
}


/*
 *----------------------------------------------------------------------
 *
 * NfsGetServer --
 *
 *      Find an NFS server in the global nfsTable.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      If the return value is TCL_OK, **nfsPtrPtr is filled with the
 *      address of the requested NFS struct.
 *
 *----------------------------------------------------------------------
 */

static int
NfsGetServer(Tcl_Interp *interp, char *name, Nfs **nfsPtrPtr)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&nfsTable, name);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such nfs server: ", name, NULL);
	return TCL_ERROR;
    }
    *nfsPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsProc --
 *
 *      NfsProc is the core callback used to process messages from
 *      RpcProc() by a sob server - see rpc.c in this directory. It
 *      switches on the recieved command from the client and supports
 *      a limited set of file functions (read, write and unlink), as
 *      well as comment board functions (post and delete), by passing
 *      members of the CData struct (defined in rpc.c) to various
 *      functions defined in this file.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    Nfs *nfsPtr = arg;
    int status, err;

    status = NS_ERROR;
    if (cmd == DCI_NFSCMDREAD) {
    	err = NfsReadFile(nfsPtr, inPtr->string, outPtr);
	if (err == 0 || err == ENOENT) {
	    status = NS_OK;
	}
    } else if ((nfsPtr->flags & DCI_NFSREADONLY)) {
    	Ns_Log(Error, "nfs[%s]: attempt to modify read-only files", nfsPtr->name);
    } else {
    	switch (cmd) {
	case DCI_NFSCMDUNLINK:
	    status = NfsUnlinkFile(nfsPtr, inPtr);
	    break;
	case DCI_NFSCMDPUT:
	    status = NfsWriteFile(nfsPtr, inPtr);
	    break;
	case DCI_NFSCMDCOPY:
	    status = NfsCopyFile(nfsPtr, inPtr);
	    break;
	case DCI_NFSCMDAPPEND:
	    status = NfsAppendFile(nfsPtr, inPtr);
	    break;
    	case DCI_NFSCMDCBPOST:
	case DCI_NFSCMDCBDEL:
	    status = NfsCbUpdate(nfsPtr, cmd == DCI_NFSCMDCBDEL ? 1 : 0, inPtr);
	    break;
	default:
	    Ns_Log(Error, "nfs[%s]: invalid command code: %d", nfsPtr->name, cmd);
	}
    }
    return status;
}
	

/*
 *----------------------------------------------------------------------
 *
 * NfsReadFile --
 *
 *      Read a file from a given sob client's DCI_NFSCMDREAD request.
 *
 * Results:
 *      Returns an integer representing the status of the read
 *      request. On success, NfsReadFile() returns 0, on failure
 *      it returns the Posix error code associated with the error. The
 *      return code of this function differs from the other sob server
 *      command processors so that more detailed error handling can
 *      be accomplished in NfsProc().
 *
 * Side effects:
 *      File data is cached in an Ns_Cache to speed subsequent lookups.
 *
 *----------------------------------------------------------------------
 */

static int
NfsReadFile(Nfs *nfsPtr, char *file, Ns_DString *dsPtr)
{
    Ns_DString path;
    struct stat st;
    File *fPtr;
    Ns_Entry *entry;
    int fd, n, new, err, trys, ok;
    Ns_Time start, finish, diff;
    Stats *statsPtr;

    Ns_GetTime(&start);
    err = 0;
    new = 1;
    fPtr = NULL;
    n = 0;
    Ns_DStringInit(&path);
    NfsPath(&path, nfsPtr, file);
    if (stat(path.string, &st) != 0) {
	err = errno;
    } else if (S_ISDIR(st.st_mode)) {
	err = EISDIR;
    } else if (!S_ISREG(st.st_mode)) {
	err = EINVAL;
    } else {
	Ns_CacheLock(nfsPtr->cache);
	entry = Ns_CacheCreateEntry(nfsPtr->cache, file, &new);
	if (!new) {
	    while (entry != NULL && (fPtr = Ns_CacheGetValue(entry)) == NULL) {
		Ns_CacheWait(nfsPtr->cache);
		entry = Ns_CacheFindEntry(nfsPtr->cache, file);
	    }
	    if (entry == NULL) {
		err = EAGAIN;
	    } else if (fPtr->st.st_size != st.st_size ||
		       fPtr->st.st_mtime != st.st_mtime) {
                Ns_CacheUnsetValue(entry);
                fPtr = NULL;
		new = 1;
	    }
	}
	if (new) {
	    Ns_CacheUnlock(nfsPtr->cache);
	    ok = 0;
	    fd = open(path.string, O_RDONLY);
	    if (fd < 0) {
	    	err = errno;
	    } else {

    		/*
		 * Attempt to get a consistant read of the file up
		 * to 10 times.
		 */

		for (trys = 0; trys < 10; ++trys) {
		    fPtr = ns_realloc(fPtr, sizeof(File) + st.st_size);
		    n = read(fd, fPtr->buf, (size_t)(st.st_size + 1));
		    if (n < 0) {
			break;
		    }
		    if (n == st.st_size) {
			ok = 1;
			break;
		    }

		    /*
		     * Either n < st.st_size meaning the file was
		     * truncated between the fstat and read.  If
		     * n = st.st_size + 1, it was extended.  Stat
		     * the file and read again.
		     */

		    if (fstat(fd, &st) != 0 || lseek(fd, 0, SEEK_SET) != 0) {
			break;
		    }
	    	}
    		if (!ok) {
		    err = errno;
		}
		close(fd);
	    }
	    Ns_CacheLock(nfsPtr->cache);
	    if (!ok) {
		if (fPtr != NULL) {
		    ns_free(fPtr);
		    fPtr = NULL;
		}
	    	Ns_CacheDeleteEntry(entry);
	    } else {
		fPtr->nfsPtr = nfsPtr;
		fPtr->ctime = start;
		fPtr->nread = 0;
	    	fPtr->st = st;
		if (n > 0 && fPtr->buf[n-1] == '\n') {
		    --n;
		}
		fPtr->bufsize = n;
		fPtr->buf[n] = '\0';
		Ns_CacheSetValueSz(entry, fPtr, fPtr->st.st_size);
	    }
	    Ns_CacheBroadcast(nfsPtr->cache);
	}
    	if (fPtr != NULL) {
	    ++fPtr->nread;
	    fPtr->atime = start;
	    Ns_DStringNAppend(dsPtr, fPtr->buf, (int)fPtr->bufsize);
	}
	Ns_CacheUnlock(nfsPtr->cache);
    }
    Ns_DStringFree(&path);
    Ns_MutexLock(&nfsPtr->statslock);
    switch (err) {
    	case 0:
	    if (!new) {
	        ++nfsPtr->nreadhit;
	    } else {
	        ++nfsPtr->nreadmiss;
            }
	    break;
    	case ENOENT:
	    ++nfsPtr->nreadnoent;
	    break;
    	default:
	    Ns_Log(Error, "read %s failed: %s", file, strerror(err));
	    ++nfsPtr->nerror;
	    break;
    }
    if (!err && (statsPtr = NfsGetStatsDetail(nfsPtr,file)) != NULL) {
        statsPtr->nread++;
        statsPtr->nbytesread += st.st_size;
        if (st.st_size > statsPtr->nmaxbytesread) {
            statsPtr->nmaxbytesread = st.st_size;
        }
        if (st.st_size < statsPtr->nminbytesread || statsPtr->nminbytesread == 0) {
            statsPtr->nminbytesread = st.st_size;
        }
        Ns_GetTime (&finish);
        Ns_DiffTime(&finish, &start, &diff);
        Ns_IncrTime(&statsPtr->nsecsread, diff.sec, diff.usec);
        if (Ns_DiffTime (&diff, &statsPtr->nmaxsecsread, NULL) == 1 ) {
            statsPtr->nmaxsecsread.sec = diff.sec;
            statsPtr->nmaxsecsread.usec = diff.usec;
        }
        if (Ns_DiffTime(&statsPtr->nminsecsread, &diff, NULL) == 1
            	|| (statsPtr->nminsecsread.sec == 0 
                && statsPtr->nminsecsread.usec == 0 )) {
            statsPtr->nminsecsread.sec = diff.sec;
            statsPtr->nminsecsread.usec = diff.usec;
        }
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    if (fDebug) {
	if (err == 0) {
	    Ns_Log(Notice, "read: %s%s", file, new ? "" : " (cached)");
	} else if (err == ENOENT) {
	    Ns_Log(Warning, "%s: %s", file, strerror(err));
	}
    }
    return err;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsGetStatsDetail --
 *
 *      Return (create if necessary) the hash table entry containing
 *      the detailed stats structure
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Stats *
NfsGetStatsDetail(Nfs *nfsPtr, char *file)
{
    Stats *statsPtr = NULL;
    Tcl_HashEntry *hPtr;
    int new;
    int argc;
    char **argv;

    if (nfsPtr->flags & DCI_NFSSTATS) {
	/*
	 * Use the first element of the path for stats key.
	 */

    	Tcl_SplitPath (file, &argc, &argv);
	if (argc > 0) {
            hPtr = Tcl_CreateHashEntry(&nfsPtr->stats, argv[0], &new);
            if (!new) {
                statsPtr = Tcl_GetHashValue(hPtr);
            } else {
                statsPtr = ns_malloc(sizeof(Stats));
                NfsResetStatsDetail(statsPtr);
                Tcl_SetHashValue(hPtr, statsPtr);
            }
        }
        ckfree ((char *) argv);
    }   
    return statsPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsResetStatsDetail --
 *
 *      Set/Reset all counters to 0 in the stats structure. 
 *      Note that this assumes calling function has taken care
 *      of necessary mutex locking.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      All values in the stats structure are reset.
 *
 *----------------------------------------------------------------------
 */

static void
NfsResetStatsDetail(Stats *statsPtr) 
{
    memset(statsPtr, 0, sizeof(Stats));
    Ns_GetTime(&statsPtr->rtime);
}


/*
 *----------------------------------------------------------------------
 *
 * NfsWriteFile --
 *
 *      Parses the client DCI_NFSCMDPUT request to pass to NfsWriteFile2().
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsWriteFile(Nfs *nfsPtr, Ns_DString *dsPtr)
{
    char **largv;
    int largc, status;
    
    largv = NULL;
    status = RPC_ERROR;
    if (Tcl_SplitList(NULL, dsPtr->string, &largc, &largv) != TCL_OK || largc != 2) {
	Ns_Log(Error, "nfs: invalid list: %s", dsPtr->string);
    } else {
	status = NfsWriteFile2(nfsPtr, largv[0], largv[1], (int)strlen(largv[1]));
    }
    if (largv != NULL) {
	ckfree((char *) largv);
    }
    Ns_MutexLock(&nfsPtr->statslock);
    if (status == RPC_OK) {
	++nfsPtr->nwrite;
    } else {
	++nfsPtr->nerror;
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsAppendFile --
 *
 *      Parses the client DCI_NFSCMDAPPEND request to pass to NfsWriteFile3().
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsAppendFile(Nfs *nfsPtr, Ns_DString *dsPtr)
{
    char **largv;
    int largc, status;
    
    largv = NULL;
    status = RPC_ERROR;
    if (Tcl_SplitList(NULL, dsPtr->string, &largc, &largv) != TCL_OK || largc != 2) {
	Ns_Log(Error, "nfs: invalid list: %s", dsPtr->string);
    } else {
	status = NfsWriteFile3(nfsPtr, largv[0], largv[1], (int)strlen(largv[1]), 1, 1);
    }
    if (largv != NULL) {
	ckfree((char *) largv);
    }
    Ns_MutexLock(&nfsPtr->statslock);
    if (status == RPC_OK) {
	++nfsPtr->nappend;
    } else {
	++nfsPtr->nerror;
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsCopyFile --
 *
 *      Parses the client DCI_NFSCMDCOPY request to pass to NfsWriteFile2().
 *	Copy handles binary data sent via nsob.copy.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
NfsCopyFile(Nfs *nfsPtr, Ns_DString *dsPtr)
{
    uint32_t *sizes;
    char *p;
    int namelen, datalen, status;
    Ns_DString ds;
    
    status = RPC_ERROR;
    sizes = (uint32_t *) dsPtr->string;
    namelen = ntohl(sizes[0]);
    datalen = ntohl(sizes[1]);
    if ((namelen + datalen + 8) != dsPtr->length) {
	Ns_Log(Error, "nfs: invalid copy lengths: %d + %d + 8 != %d",
	    namelen, datalen, dsPtr->length);
    } else {
	Ns_DStringInit(&ds);
	p = dsPtr->string + 8;
	Ns_DStringNAppend(&ds, p, namelen);
	p += namelen;
	status = NfsWriteFile3(nfsPtr, ds.string, p, datalen, 0, 0);
	Ns_DStringFree(&ds);
    }
    Ns_MutexLock(&nfsPtr->statslock);
    if (status == RPC_OK) {
	++nfsPtr->ncopy;
    } else {
	++nfsPtr->nerror;
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsUnlinkFile --
 *
 *      Handles a sob client's DCI_NFSCMDUNLINK request by calling unlink.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      On success, removes the file from the filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
NfsUnlinkFile(Nfs *nfsPtr, Ns_DString *dsPtr)
{
    Ns_DString path;
    int status;
    char *file;

    status = RPC_OK;
    file = dsPtr->string;
    Ns_DStringInit(&path);
    NfsPath(&path, nfsPtr, file);
    if (unlink(path.string) != 0) {
        status = errno;
        Ns_MutexLock(&nfsPtr->statslock);
        ++nfsPtr->nerror;
        Ns_MutexUnlock(&nfsPtr->statslock);
	Ns_Log(Error, "nfs: unlink(%s) failed: %s", path.string, strerror(errno));
    } else {
        Ns_MutexLock(&nfsPtr->statslock);
        ++nfsPtr->nunlink;
        Ns_MutexUnlock(&nfsPtr->statslock);
	NfsFlush(nfsPtr, file);
    }
    Ns_DStringFree(&path);
    if (fDebug) {
	Ns_Log(status == NS_OK ? Notice : Error, "unlink: %s %s",
	    file, status == NS_OK ? "ok" : "failed");
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsCbUpdate --
 *
 *      This is a server command used to handle the client's POST
 *	and DELETE.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Appends message to comment board, creating the board if no 
 *      messages exist. The data is saved to disk.
 *
 *----------------------------------------------------------------------
 */

static int
NfsCbUpdate(Nfs *nfsPtr, int delete, Ns_DString *dsPtr)
{
    char **pv, *board;
    int pc, status, id;
    Ns_DString ds;

    pv = NULL;
    status = RPC_ERROR;
    Ns_DStringInit(&ds);
    if (Tcl_SplitList(NULL, dsPtr->string, &pc, &pv) != TCL_OK) {
	goto done;
    }
    if (!delete && pc != 4) {
	Ns_Log(Error, "ncb: invalid post: %s", dsPtr->string);
	goto done;
    } else if (delete && (pc != 2 || Tcl_GetInt(NULL, pv[1], &id) != TCL_OK)) {
	Ns_Log(Error, "ncb: invalid delete: %s", dsPtr->string);
	goto done;
    }
    board = pv[0];
    status = NfsReadFile(nfsPtr, board, &ds);
    if (status == 0 || status == ENOENT) {
	if (delete) {
            status = Dci_CbDelete(board, &ds, id);
	} else {
            status = Dci_CbPost(board, &ds, pv[1], pv[2], pv[3], nfsPtr->maxposts);
	}
	if (status == RPC_OK) {
	    status = NfsWriteFile2(nfsPtr, board, ds.string, ds.length);
	}
    }
done:
    if (pv) ckfree((char *) pv);
    Ns_DStringTrunc(dsPtr, 0);
    Ns_MutexLock(&nfsPtr->statslock);
    if (status == RPC_OK) {
	if (delete) {
	    ++nfsPtr->ncbdel;
	} else {
	    ++nfsPtr->ncbpost;
	}
    } else {
	++nfsPtr->nerror;
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    return status;
}
    

/*
 *----------------------------------------------------------------------
 *
 * NfsWriteFile2, NfsWriteFile3 --
 *
 *      Writes data to the named file of the specified length. This is
 *      a support function for the NFS callbacks.
 *
 *      NB: For full write, data is written to a temp file and
 *      renamed in order to use the transaction capabilities of the
 *      filesystem.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Data (bytes) is written to a file.
 *
 *----------------------------------------------------------------------
 */
	
static int
NfsWriteFile2(Nfs *nfsPtr, char *file, char *bytes, int len)
{
    return NfsWriteFile3(nfsPtr, file, bytes, len, 1, 0);
}

static int
NfsWriteFile3(Nfs *nfsPtr, char *key, char *bytes, int len, int nl, int append)
{
    Ns_DString path, tmp;
    int fd, status, flags;
    char uuid[32], *file;
    struct iovec iov[2];
    Stats *statsPtr;
    Ns_Time start, finish, diff;

    Ns_GetTime(&start);
    status = RPC_ERROR;
    Ns_DStringInit(&path);
    Ns_DStringInit(&tmp);
    file = NfsPath(&path, nfsPtr, key);
    if ((nfsPtr->flags & DCI_NFSMKDIRS) && !NfsMkdirs(file)) {
	Ns_Log(Error, "nfs: mkdirs(%s) failed: %s", file, strerror(errno));
    } else if ((nfsPtr->flags & DCI_NFSRENAME) && !append && !Dci_GetUuid(uuid)) {
	Ns_Log(Error, "nfs: could not create uuid");
    } else {
	flags = O_WRONLY|O_CREAT;
	if (append) {
	    flags |= O_APPEND;
	} else if (nfsPtr->flags & DCI_NFSRENAME) {
	    Ns_DStringVarAppend(&tmp, file, ".nfstmp.", uuid, NULL);
	    file = tmp.string;
	    flags |= O_EXCL;
	} else {
            flags |= O_TRUNC;
        }
	fd = open(file, flags, 0644);
	if (fd < 0) {
            status = errno;
	    Ns_Log(Error, "nfs: open(%s) failed: %s", file, strerror(errno));
	} else {
	    iov[0].iov_base = bytes;
	    iov[0].iov_len = len;
	    if (nl && len > 0 && bytes[len-1] != '\n') {
		iov[1].iov_base = "\n";
		iov[1].iov_len = 1;
		++len;
	    } else {
		iov[1].iov_base = NULL;
		iov[1].iov_len = 0;
	    }
	    if (writev(fd, iov, 2) != len) {
                status = errno;
	    	Ns_Log(Error, "nfs: writev(%s) failed: %s", file, strerror(errno));
	    } else {
		if (file == path.string) {
		    status = RPC_OK;
		} else {
	    	    if ((status = rename(file, path.string)) != 0) {
	    		Ns_Log(Error, "nfs: rename(%s, %s) failed: %s", file,
                               path.string, strerror(errno));
	    	    } else {
			status = RPC_OK;
		    }
		}
	    }
    	    close(fd);
    	}
    }
    if (fDebug) {
	Ns_Log(status == NS_OK ? Notice : Error, "%s: %s - %s",
	    append ? "append" : "write", path.string, status == NS_OK ? "ok" : "failed");
    }

    Ns_MutexLock(&nfsPtr->statslock);
    statsPtr = NfsGetStatsDetail(nfsPtr, key);
    if (statsPtr != NULL) {
        statsPtr->nwrite++;
        statsPtr->nbyteswrite += len;
        if (len > statsPtr->nmaxbyteswrite) {
            statsPtr->nmaxbyteswrite = len;
        }
        if (len < statsPtr->nminbyteswrite || statsPtr->nminbyteswrite == 0) {
            statsPtr->nminbyteswrite = len;
        }
        Ns_GetTime(&finish);
        Ns_DiffTime(&finish, &start, &diff);
        Ns_IncrTime(&statsPtr->nsecswrite, diff.sec, diff.usec);
        if (Ns_DiffTime (&diff, &statsPtr->nmaxsecswrite, NULL) == 1 ) {
            statsPtr->nmaxsecswrite.sec = diff.sec;
            statsPtr->nmaxsecswrite.usec = diff.usec;
        }
        if (Ns_DiffTime(&statsPtr->nminsecswrite, &diff, NULL) == 1
            	|| (statsPtr->nminsecswrite.sec == 0 
                && statsPtr->nminsecswrite.usec == 0)) {
            statsPtr->nminsecswrite.sec = diff.sec;
            statsPtr->nminsecswrite.usec = diff.usec;
        }
    }
    Ns_MutexUnlock(&nfsPtr->statslock);
    Ns_DStringFree(&path);
    Ns_DStringFree(&tmp);
    if (status == RPC_OK) {
	NfsFlush(nfsPtr, key);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsPath --
 *
 *      Builds a full, normalized path for a requested file, given
 *      the file and base directory of the sob server. This function
 *      is called from the base Nfs functions: NfsReadFile(), 
 *      NfsWriteFile2() and NfsUnlinkFile().
 *
 * Results:
 *      Pointer to normalized path.
 *
 * Side effects:
 *      dsPtr is updated - potentially grown - to contain the 
 *      normalized path.
 *
 *----------------------------------------------------------------------
 */

static char *
NfsPath(Ns_DString *dsPtr, Nfs *nfsPtr, char *file)
{
    Ns_DString norm;

    Ns_DStringInit(&norm);
    Ns_NormalizePath(&norm, file);
    Ns_MakePath(dsPtr, nfsPtr->root, norm.string, NULL);
    Ns_DStringFree(&norm);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * NfsMkdirs --
 *
 *      Recursively create directories for NFS file.
 *
 * Results:
 *      See Dci_Mkdirs.
 *
 * Side effects:
 *      Directories, if they don't exist, are created.
 *
 *----------------------------------------------------------------------
 */

static int
NfsMkdirs(char *dir)
{
    char *p;
    int status;

    p = strrchr(dir, '/');
    if (p == NULL) {
	errno = EINVAL;
	status = NS_ERROR;
    } else {
	*p = '\0';
	status = Dci_MkDirs(dir, 0755);
	*p = '/';
    }
    return (status == NS_OK ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NfsFlush --
 *
 *      NfsFlush removes an entry from the file cache. File caches are
 *      private to the Nfs instance, but used by all sob
 *      servers. If configured, NfsFlush also triggers a network cache
 *      flush via Dci_NcfSend() - see flush.c in this directory.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes cached file and if configured, sends a network flush
 *      message to all sob clients via Dci_NcfSend().
 *
 *----------------------------------------------------------------------
 */

static void
NfsFlush(Nfs *nfsPtr, char *file)
{
    Ns_Entry *entry;
    
    Ns_CacheLock(nfsPtr->cache);
    entry = Ns_CacheFindEntry(nfsPtr->cache, file);
    if (entry != NULL) {
    	Ns_CacheFlushEntry(entry);
    }
    Ns_CacheUnlock(nfsPtr->cache);
    if (nfsPtr->flushcache != NULL) {
	Dci_NcfSend(nfsPtr->flushcache, file);
    }
}
