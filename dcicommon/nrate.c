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

#define NRATE_MAXNAME 32
#define NRATE_MAXPRECISION 6
#define NRATE_FILE "nrate.dat"
#define NRATE_CMDADD 1

typedef struct {
    uint32_t count;
    u_long   total;
} Rating;

typedef struct {
    char *file;
    GDBM_FILE db;
    Ns_Cache *cache;
} SData;

typedef struct {
    char *name;
    int dirty;
    SData *sdPtr;
    Rating rating;
} NetRate;

typedef struct {
    int16_t adjCountBy;
    int16_t adjTotalBy;
    char name[NRATE_MAXNAME+1];
} Msg;

static Tcl_CmdProc NratecAddCmd;
static Ns_TclInterpInitProc AddClientCmds;
static void NrateResult(Tcl_Interp *interp, Rating *ratingPtr, int nPrecision);
static void NratesAdd(SData *sdPtr, char *name, int adjCountBy, int adjTotalBy, Rating *ratingPtr);
static Dci_RpcProc NratesProc;
static Ns_Callback NratesFlusher;
static Ns_Callback NratesFreeEntry;
static void NratesFlushEntry(NetRate *netratePtr);
static Tcl_CmdProc NratesDeleteCmd;
static Tcl_CmdProc NratesFindCmd;
static Tcl_CmdProc NratesDatabaseCmd;
static Tcl_CmdProc NratesAddCmd;
static Tcl_CmdProc NratesBackupCmd;
static Ns_TclInterpInitProc AddServerCmds;
static int fDebug;


int
DciNrateInit(char *server, char *module)
{
    SData *sdPtr;
    Dci_Rpc *rpc;
    Ns_Set *set;
    char *path, name[100];
    int i;

    Dci_LogIdent(module, rcsid);

    path = Ns_ConfigGetPath(server, module, "nrate", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
        fDebug = 0;
    }
    path = Ns_ConfigGetPath(server, module, "nrate/client", NULL);
    if (path != NULL) {
        if (!Ns_ConfigGetInt(path, "timeout", &i) || i < 1) {
            i = 2;
        }
        rpc = Dci_RpcCreateClient(server, module, "nrate", i);
        if (rpc == NULL) {
            return NS_ERROR;
        }
        Ns_TclInitInterps(server, AddClientCmds, rpc);
    }
    path = Ns_ConfigGetPath(server, module, "nrate/server/clients", NULL);
    set = Ns_ConfigGetSection(path);
    if (set != NULL) {
        sdPtr = ns_malloc(sizeof(SData));
        path = Ns_ConfigGetPath(server, module, "nrate/server", NULL);

        /*
         * Open the ratings database.
         */
        sdPtr->file = Ns_ConfigGetValue(path, "database");
        if (sdPtr->file == NULL) {
            Ns_DString ds;

            Ns_DStringInit(&ds);
            Ns_ModulePath(&ds, server, module, "nrate", NULL);
            if (mkdir(ds.string, 0755) != 0 && errno != EEXIST) {
                Ns_Log(Error, "nrates: mkdir(%s) failed: %s", 
                ds.string, strerror(errno));
                return NS_ERROR;
            }
            Ns_DStringAppend(&ds, "/nrate.dat");
            sdPtr->file = Ns_DStringExport(&ds);
        }
        sdPtr->db = Dci_GdbmOpen(sdPtr->file);
        if (sdPtr->db == NULL) {
            Ns_Log(Error, "nrates: gdbm_open(%s) failed: %s", 
                   sdPtr->file, gdbm_strerror(gdbm_errno));
            return NS_ERROR;
        }

        /*
         * Create the cache and the schedule flush procedure.
         */
        if (!Ns_ConfigGetInt(path, "cachesize", &i) || i < 1) {
            i = 5 * 1024 * 1000;    /* 5 megs. */
        }
        sprintf(name, "nrates:%s", server);
        sdPtr->cache = Ns_CacheCreateSz(name, TCL_STRING_KEYS, (size_t)i, NratesFreeEntry);
        if (!Ns_ConfigGetInt(path, "interval", &i) || i < 1) {
            i = 120;
        }
        Ns_ScheduleProc(NratesFlusher, sdPtr, 0, i);
        Ns_RegisterAtExit(NratesFlusher, sdPtr);
        if (Dci_RpcCreateServer(server, module, "nrate", NULL, set, NratesProc,
				sdPtr) != NS_OK) {
            return NS_ERROR;
        }
        Ns_TclInitInterps(server, AddServerCmds, sdPtr);
    }
    return NS_OK;
}


static int
AddClientCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nratec.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nratec.add", NratecAddCmd, arg, NULL);
    return NS_OK;
}


static int
AddServerCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nrates.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nrates.delete", NratesDeleteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nrates.find", NratesFindCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nrates.add", NratesAddCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nrates.backup", NratesBackupCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nrates.database", NratesDatabaseCmd, arg, NULL);
    return TCL_OK;
}


static int
NratecAddCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc = arg;
    Rating *ratingPtr;
    Msg *msgPtr;
    Ns_DString in, out;
    int nCountAdjust, nTotalAdjust, nPrecision;
    int nResult;
    
    if (argc != 5) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " key countAdjust totalAdjust precision\"", NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[1]) > NRATE_MAXNAME) {
        Tcl_AppendResult(interp, "nrate name too long: ", argv[1], NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &nCountAdjust) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &nTotalAdjust) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[4], &nPrecision) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nPrecision > NRATE_MAXPRECISION) {
        nPrecision = NRATE_MAXPRECISION;
    }
    if (nPrecision < 0) {
        nPrecision = 0;
    }
    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    Ns_DStringSetLength(&in, sizeof(Msg));
    msgPtr = (Msg *) in.string;
    msgPtr->adjCountBy = htons((uint16_t) nCountAdjust);
    msgPtr->adjTotalBy = htons((uint16_t) nTotalAdjust);
    strcpy(msgPtr->name, argv[1]);
    nResult = Dci_RpcSend(rpc, NRATE_CMDADD, &in, &out);
    if (nResult != RPC_OK) {
        Tcl_AppendResult(interp, "could not send rating: ", argv[1], 
            " : ", Dci_RpcTclError(interp, nResult), NULL);
    } else {
        ratingPtr = (Rating *) out.string;
        ratingPtr->count = ntohl(ratingPtr->count);
        ratingPtr->total = ntohl(ratingPtr->total);
        NrateResult(interp, ratingPtr, nPrecision);
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return (nResult == RPC_OK ? TCL_OK : TCL_ERROR);
}


static int
NratesProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    SData *sdPtr = arg;
    Msg *msgPtr;
    Rating *ratingPtr;
    
    if (cmd != NRATE_CMDADD || inPtr->length != sizeof(Msg)) {
        Ns_Log(Error, "nrates: invalid request");
        return NS_ERROR;
    }
    msgPtr = (Msg *) inPtr->string;
    msgPtr->adjCountBy = ntohs(msgPtr->adjCountBy);
    msgPtr->adjTotalBy = ntohs(msgPtr->adjTotalBy);
    if (fDebug) {
        Ns_Log(Notice, "nrates: key %s, adjCountBy %d, adjTotalBy %d", msgPtr->name, msgPtr->adjCountBy, msgPtr->adjTotalBy);
    }
    Ns_DStringSetLength(outPtr, sizeof(Rating));
    ratingPtr = (Rating *) outPtr->string;
    NratesAdd(sdPtr, msgPtr->name, msgPtr->adjCountBy, msgPtr->adjTotalBy, ratingPtr);
    ratingPtr->count = htonl(ratingPtr->count);
    ratingPtr->total = htonl(ratingPtr->total);
    return NS_OK;
}


static void
NratesFreeEntry(void *arg)
{
    NetRate *netratePtr = (NetRate *) arg;

    if (netratePtr->dirty) {
        NratesFlushEntry(netratePtr);
        gdbm_sync(netratePtr->sdPtr->db);
    }
    ns_free(netratePtr);
}


static void
NratesFlushEntry(NetRate *netratePtr)
{
    datum key, content;

    key.dptr = netratePtr->name;
    key.dsize = strlen(key.dptr);
    content.dptr = (char *) &netratePtr->rating;
    content.dsize = sizeof(Rating);
    if (gdbm_store(netratePtr->sdPtr->db, key, content, GDBM_REPLACE) != 0) {
        Ns_Log(Error, "nrates: gdbm_store(%s) failed: %s",
            key.dptr, gdbm_strerror(gdbm_errno));
    } else {
        netratePtr->dirty = 0;
    }
}


static void
NratesFlushRatings(SData *sdPtr)
{
    Ns_Entry *entPtr;
    NetRate *netratePtr; 
    Ns_CacheSearch search;

    entPtr = Ns_CacheFirstEntry(sdPtr->cache, &search);
    while (entPtr != NULL) {
        netratePtr = (NetRate *) Ns_CacheGetValue(entPtr);
        if (netratePtr->dirty) {
            NratesFlushEntry(netratePtr);
        }
        entPtr = Ns_CacheNextEntry(&search);
    }
    gdbm_sync(sdPtr->db);
}


static void
NratesFlusher(void *arg)
{
    SData *sdPtr = arg;

    Ns_CacheLock(sdPtr->cache);
    NratesFlushRatings(sdPtr);
    Ns_CacheUnlock(sdPtr->cache);
}


static void
NratesAdd(SData *sdPtr, char *name, int adjCountBy, int adjTotalBy, Rating *ratingPtr)
{
    NetRate *netratePtr;
    int new;
    Ns_Entry *entPtr;
    datum key, content;
    int nCount ,nTotal;

    Ns_CacheLock(sdPtr->cache);
    entPtr = Ns_CacheCreateEntry(sdPtr->cache, name, &new);
    if (!new) {
        netratePtr = Ns_CacheGetValue(entPtr);
    } else {
        netratePtr = ns_calloc(1, sizeof(NetRate));
        netratePtr->sdPtr = sdPtr;
        netratePtr->name = Ns_CacheKey(entPtr);
        key.dptr = netratePtr->name;
        key.dsize = strlen(key.dptr);
        content = gdbm_fetch(sdPtr->db, key);
        if (content.dptr != NULL) {
            if (content.dsize > sizeof(Rating)) {
                content.dsize = sizeof(Rating);
            }
            memcpy(&netratePtr->rating, content.dptr, (size_t)content.dsize);
            gdbm_free(content.dptr);
        }
        Ns_CacheSetValueSz(entPtr, netratePtr, sizeof(NetRate));
    }

    /* we need these local versions to be ints because the unsigned ints cant hold 
     * negative numbers
     */
    nCount = netratePtr->rating.count;
    nTotal = netratePtr->rating.total;

    nCount = nCount + adjCountBy;
    nTotal = nTotal + adjTotalBy;
    if (nCount < 0) {
        netratePtr->rating.count = 0; 
    } else {
        netratePtr->rating.count = nCount;
    }

    if (nTotal < 0) {
        netratePtr->rating.total = 0; 
    } else {
        netratePtr->rating.total = nTotal;
    }

    netratePtr->dirty = 1;

    memcpy(ratingPtr, &netratePtr->rating, sizeof(Rating));
    Ns_CacheUnlock(sdPtr->cache);
}    


static int
NratesDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    Ns_Entry *entPtr;
    datum key;
    int ret;
    char buf[20];

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " poll\"", NULL);
        return TCL_ERROR;
    }
    key.dptr = argv[1];
    key.dsize = strlen(key.dptr);
    Ns_CacheLock(sdPtr->cache);
    entPtr = Ns_CacheFindEntry(sdPtr->cache, argv[1]);
    if (entPtr != NULL) {
        ns_free(Ns_CacheGetValue(entPtr));
        Ns_CacheDeleteEntry(entPtr);
    }
    ret = gdbm_delete(sdPtr->db, key);
    Ns_CacheUnlock(sdPtr->cache);
    sprintf(buf, "%d", ret);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
NratesFindCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    datum key;
    char name[NRATE_MAXNAME+1];
    char *p;

    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    Ns_CacheLock(sdPtr->cache);
    key = gdbm_firstkey(sdPtr->db);
    while (key.dptr) {
        if (key.dsize <= NRATE_MAXNAME) {
            strncpy(name, key.dptr, (size_t)key.dsize);
            name[key.dsize] = '\0';
            if (argc == 1 || Tcl_StringMatch(name, argv[1])) {
                Tcl_AppendElement(interp, name);
            }
        }
        p = key.dptr;
        key = gdbm_nextkey(sdPtr->db, key);
        gdbm_free(p);
    }
    Ns_CacheUnlock(sdPtr->cache);
    return TCL_OK;
}


static int
NratesAddCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    Rating rating;
    int nCountAdjust, nTotalAdjust, nPrecision;

    if (argc != 5 ) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " key countAdjust totalAdjust precision\"", NULL);
        return TCL_ERROR;
    }
    if (strlen(argv[1]) >= NRATE_MAXNAME) {
        Tcl_AppendResult(interp, "nrate name too long: ", argv[1], NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &nCountAdjust) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &nTotalAdjust) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[4], &nPrecision) != TCL_OK) {
        return TCL_ERROR;
    } 
    if (nPrecision > NRATE_MAXPRECISION) {
        nPrecision = NRATE_MAXPRECISION;
    }
    if (nPrecision < 0) {
        nPrecision = 0;
    }
    NratesAdd(sdPtr, argv[1], nCountAdjust, nTotalAdjust, &rating);
    NrateResult(interp, &rating, nPrecision);
    return TCL_OK;
}


static int
NratesDatabaseCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;

    if (argc != 1) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], "\"", NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, sdPtr->file, TCL_STATIC);
    return TCL_OK;
}


static int
NratesBackupCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
#define BUFSIZE 8192
    SData *sdPtr = arg;
    char buf[BUFSIZE];
    int n, code, in, out;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " backupFile ?maxroll?\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 3) {
        if (Tcl_GetInt(interp, argv[2], &n) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Ns_RollFile(argv[1], n) != NS_OK) {
            Tcl_AppendResult(interp, "could not roll \"", argv[1],
                "\"", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
        }
    }
    in = open(sdPtr->file, O_RDONLY);
    if (in <= 0) {
        Tcl_AppendResult(interp, "could not open \"", sdPtr->file,
            "\" for read: ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    out = open(argv[1], O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (out <= 0) {
        close(in);
        Tcl_AppendResult(interp, "could not open \"", argv[1],
            "\" for write: ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    code = TCL_ERROR;
    Ns_CacheLock(sdPtr->cache);
    NratesFlushRatings(sdPtr);
    while ((n = read(in, buf, BUFSIZE)) > 0) {
        if (write(out, buf, (size_t)n) != n) {
            Tcl_AppendResult(interp, "write to \"", argv[1],
                "\" failed: ", Tcl_PosixError(interp), NULL);
            break;
        }
    }
    Ns_CacheUnlock(sdPtr->cache);
    if (n == 0) {
        code = TCL_OK;
    } else if (n < 0) {
        Tcl_AppendResult(interp, "read from \"", sdPtr->file,
            "\" failed: ", Tcl_PosixError(interp), NULL);
    }
    close(in);
    close(out);
    return code;
}


static void
NrateResult(Tcl_Interp *interp, Rating *ratingPtr, int nPrecision)
{
    unsigned int count, total;
    char buf[15];
    char fmat[15];
    char fmat1[15];
    float rating;

    count=ratingPtr->count;
    total=ratingPtr->total;

    sprintf(buf, "%d ", count);
    Tcl_AppendResult(interp, buf, NULL);
    sprintf(buf, "%d ", total);
    Tcl_AppendResult(interp, buf, NULL);

    if ( count == 0 || total == 0) {
        rating = 0.0;
    } else {
        rating = (float) total/ (float) count;
    }

    sprintf(fmat1, "%d", nPrecision);
    strcpy(fmat, "%.");
    strcat(fmat, fmat1);
    strcat(fmat, "f");
    
    sprintf(buf, fmat, rating);
    Tcl_AppendResult(interp, buf, NULL);
}
