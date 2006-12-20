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

#define POLL_MAXNAME 32
#define POLL_MAXCHOICE 20
#define POLL_FILE "votes.dat"
#define POLL_CMDVOTE 1
#define POLL2_CMDVOTE  2

typedef struct {
    uint32_t counts[POLL_MAXCHOICE];
} Votes;

typedef struct {
    char *file;
    GDBM_FILE db;
    Ns_Cache *cache;
} SData;

typedef struct {
    char *name;
    int dirty;
    SData *sdPtr;
    Votes votes;
} Poll;

typedef struct {
    uint16_t choice;
    char name[POLL_MAXNAME+1];
} Msg;

typedef struct {
    uint16_t choice;
    char     name[POLL_MAXNAME+1];
    int16_t  adjVoteBy;
} Msg2;

#define GetVote(votesPtr,index) ((votesPtr)->counts[index])
static Tcl_CmdProc NpcVoteCmd;
static Tcl_CmdProc Npc2VoteCmd;
static Ns_TclInterpInitProc AddClientCmds;
static Ns_TclInterpInitProc AddClientCmds2;
static void NpResult(Tcl_Interp *interp, Votes *votesPtr);
static void NpsVote(SData *sdPtr, char *board, int choice, Votes *votesPtr);
static void Nps2Vote(SData *sdPtr, char *board, int choice, int adjVoteBy, Votes *votesPtr);
static Dci_RpcProc NpsProc;
static Dci_RpcProc Nps2Proc;
static Ns_Callback NpsFlusher;
static Ns_Callback NpsFreeEntry;
static void NpsFlushEntry(Poll *pollPtr);
static Tcl_CmdProc NpsDeleteCmd;
static Tcl_CmdProc NpsFindCmd;
static Tcl_CmdProc NpsDatabaseCmd;
static Tcl_CmdProc NpsVoteCmd;
static Tcl_CmdProc Nps2VoteCmd;
static Tcl_CmdProc NpsBackupCmd;
static Ns_TclInterpInitProc AddServerCmds;
static int fDebug;


int
DciNpInit(char *server, char *module)
{
    SData *sdPtr;
    Dci_Rpc *rpc;
    Dci_Rpc *rpc2;
    Ns_Set *set;
    char *path, name[100];
    int i;

    DciAddIdent(rcsid);
    path = Ns_ConfigGetPath(server, module, "np", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
    	fDebug = 0;
    }
    path = Ns_ConfigGetPath(server, module, "np/client", NULL);
    if (path != NULL) {
	if (!Ns_ConfigGetInt(path, "timeout", &i) || i < 1) {
    	    i = 2;
	}
	rpc = Dci_RpcCreateClient(server, module, "np", i);
	if (rpc == NULL) {
	    return NS_ERROR;
	}

	rpc2 = Dci_RpcCreateClient(server, module, "np2", i);
	if (rpc2 == NULL) {
	    return NS_ERROR;
	}

    	Ns_TclInitInterps(server, AddClientCmds, rpc);
    	Ns_TclInitInterps(server, AddClientCmds2, rpc2);
    }
    path = Ns_ConfigGetPath(server, module, "np/server/clients", NULL);
    set = Ns_ConfigGetSection(path);
    if (set != NULL) {
	sdPtr = ns_malloc(sizeof(SData));
	path = Ns_ConfigGetPath(server, module, "np/server", NULL);

	/*
	 * Open the votes database.
	 */

	sdPtr->file = Ns_ConfigGetValue(path, "database");
	if (sdPtr->file == NULL) {
    	    Ns_DString ds;

	    Ns_DStringInit(&ds);
	    Ns_ModulePath(&ds, server, module, "np", NULL);
	    if (mkdir(ds.string, 0755) != 0 && errno != EEXIST) {
		Ns_Log(Error, "nps: mkdir(%s) failed: %s", 
	    	    ds.string, strerror(errno));
		return NS_ERROR;
	    }
	    Ns_DStringAppend(&ds, "/votes.dat");
	    sdPtr->file = Ns_DStringExport(&ds);
	}
	sdPtr->db = Dci_GdbmOpen(sdPtr->file);
	if (sdPtr->db == NULL) {
	    Ns_Log(Error, "nps: gdbm_open(%s) failed: %s", 
		sdPtr->file, gdbm_strerror(gdbm_errno));
	    return NS_ERROR;
	}

	/*
	 * Create the cache and the schedule flush procedure.
	 */

	if (!Ns_ConfigGetInt(path, "cachesize", &i) || i < 1) {
	    i = 5 * 1024 * 1000;    /* 5 megs. */
	}
	sprintf(name, "nps:%s", server);
	sdPtr->cache = Ns_CacheCreateSz(name, TCL_STRING_KEYS, (size_t)i, NpsFreeEntry);
	if (!Ns_ConfigGetInt(path, "interval", &i) || i < 1) {
	    i = 120;
	}
	Ns_ScheduleProc(NpsFlusher, sdPtr, 0, i);
	Ns_RegisterAtExit(NpsFlusher, sdPtr);
    	if (Dci_RpcCreateServer(server, module, "np", NULL, set, NpsProc,
				sdPtr) != NS_OK) {
	    return NS_ERROR;
	}
    	if (Dci_RpcCreateServer(server, module, "np2", NULL, set, Nps2Proc,
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
    Tcl_CreateCommand(interp, "npc.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "npc.vote", NpcVoteCmd, arg, NULL);
    return NS_OK;
}

static int
AddClientCmds2(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "npc2.vote", Npc2VoteCmd, arg, NULL);
    return NS_OK;
}



static int
AddServerCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "nps.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "nps.delete", NpsDeleteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nps.find", NpsFindCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nps.vote", NpsVoteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nps2.vote", Nps2VoteCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nps.backup", NpsBackupCmd, arg, NULL);
    Tcl_CreateCommand(interp, "nps.database", NpsDatabaseCmd, arg, NULL);
    return TCL_OK;
}


static int
NpcVoteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc = arg;
    Votes *votesPtr;
    Msg *msgPtr;
    Ns_DString in, out;
    int n, i;
    
    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " poll choice\"", NULL);
	return TCL_ERROR;
    }
    if (strlen(argv[1]) > POLL_MAXNAME) {
    	Tcl_AppendResult(interp, "invalid poll: ", argv[1], NULL);
    	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &n) != TCL_OK) {
    	return TCL_ERROR;
    }
    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    Ns_DStringSetLength(&in, sizeof(Msg));
    msgPtr = (Msg *) in.string;
    msgPtr->choice = htons((uint16_t) n);
    strcpy(msgPtr->name, argv[1]);
    n = Dci_RpcSend(rpc, POLL_CMDVOTE, &in, &out);
    if (n != RPC_OK) {
    	Tcl_AppendResult(interp, "could not vote in poll: ", argv[1], 
            " : ", Dci_RpcTclError(interp, n), NULL);
    } else {
	votesPtr = (Votes *) out.string;
	for (i = 0; i < POLL_MAXCHOICE; ++i) {
	    votesPtr->counts[i] = ntohl(votesPtr->counts[i]);
	}
	NpResult(interp, votesPtr);
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return (n == RPC_OK ? TCL_OK : TCL_ERROR);
}


static int
Npc2VoteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_Rpc *rpc2 = arg;
    Votes *votesPtr;
    Msg2 *msgPtr;
    Ns_DString in, out;
    int n, i, adjVoteBy;
    
    if (argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " poll choice adjust\"", NULL);
	return TCL_ERROR;
    }
    if (strlen(argv[1]) > POLL_MAXNAME) {
    	Tcl_AppendResult(interp, "invalid poll: ", argv[1], NULL);
    	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &n) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[3], &adjVoteBy) != TCL_OK) {
    	return TCL_ERROR;
    }
    Ns_DStringInit(&in);
    Ns_DStringInit(&out);
    Ns_DStringSetLength(&in, sizeof(Msg2));
    msgPtr = (Msg2 *) in.string;
    msgPtr->choice = htons((uint16_t) n);
    msgPtr->adjVoteBy = htons((int16_t) adjVoteBy);
    strcpy(msgPtr->name, argv[1]);
    n = Dci_RpcSend(rpc2, POLL2_CMDVOTE, &in, &out);
    if (n != RPC_OK) {
    	Tcl_AppendResult(interp, "could not vote in poll: ", argv[1], 
            " : ", Dci_RpcTclError(interp, n), NULL);
    } else {
	votesPtr = (Votes *) out.string;
	for (i = 0; i < POLL_MAXCHOICE; ++i) {
	    votesPtr->counts[i] = ntohl(votesPtr->counts[i]);
	}
	NpResult(interp, votesPtr);
    }
    Ns_DStringFree(&in);
    Ns_DStringFree(&out);
    return (n == RPC_OK ? TCL_OK : TCL_ERROR);
}




static int
NpsProc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    SData *sdPtr = arg;
    Msg *msgPtr;
    Votes *votesPtr;
    int i;
    
    if (cmd != POLL_CMDVOTE || inPtr->length != sizeof(Msg)) {
    	Ns_Log(Error, "invalid request");
    	return NS_ERROR;
    }
    msgPtr = (Msg *) inPtr->string;
    msgPtr->choice = ntohs(msgPtr->choice);
    if (fDebug) {
    	Ns_Log(Notice, "nps: vote %s = %d", msgPtr->name, msgPtr->choice);
    }
    Ns_DStringSetLength(outPtr, sizeof(Votes));
    votesPtr = (Votes *) outPtr->string;
    NpsVote(sdPtr, msgPtr->name, msgPtr->choice, votesPtr);
    for (i = 0; i < POLL_MAXCHOICE; ++i) {
	votesPtr->counts[i] = htonl(votesPtr->counts[i]);
    }
    return NS_OK;
}


static int
Nps2Proc(void *arg, int cmd, Ns_DString *inPtr, Ns_DString *outPtr)
{
    SData *sdPtr = arg;
    Msg2 *msgPtr;
    Votes *votesPtr;
    int i;
    
    if (cmd != POLL2_CMDVOTE || inPtr->length != sizeof(Msg2)) {
    	Ns_Log(Error, "invalid request");
    	return NS_ERROR;
    }
    msgPtr = (Msg2 *) inPtr->string;
    msgPtr->choice = ntohs(msgPtr->choice);
    msgPtr->adjVoteBy = ntohs(msgPtr->adjVoteBy);
    if (fDebug) {
	Ns_Log(Notice, "nps: vote %s = %d adjust by is: %d ",
            msgPtr->name, msgPtr->choice, msgPtr->adjVoteBy);
    }
    Ns_DStringSetLength(outPtr, sizeof(Votes));
    votesPtr = (Votes *) outPtr->string;
    Nps2Vote(sdPtr, msgPtr->name, msgPtr->choice, msgPtr->adjVoteBy, votesPtr);
    for (i = 0; i < POLL_MAXCHOICE; ++i) {
	votesPtr->counts[i] = htonl(votesPtr->counts[i]);
    }
    return NS_OK;
}


static void
NpsFreeEntry(void *arg)
{
    Poll *pollPtr = (Poll *) arg;

    if (pollPtr->dirty) {
	NpsFlushEntry(pollPtr);
        gdbm_sync(pollPtr->sdPtr->db);
    }
    ns_free(pollPtr);
}


static void
NpsFlushEntry(Poll *pollPtr)
{
    datum key, content;

    key.dptr = pollPtr->name;
    key.dsize = strlen(key.dptr);
    content.dptr = (char *) &pollPtr->votes;
    content.dsize = sizeof(Votes);
    if (gdbm_store(pollPtr->sdPtr->db, key, content, GDBM_REPLACE) != 0) {
	Ns_Log(Error, "nps: gdbm_store(%s) failed: %s",
	    key.dptr, gdbm_strerror(gdbm_errno));
    } else {
	pollPtr->dirty = 0;
    }
}


static void
NpsFlushVotes(SData *sdPtr)
{
    Ns_Entry *entPtr;
    Poll *pollPtr; 
    Ns_CacheSearch search;

    entPtr = Ns_CacheFirstEntry(sdPtr->cache, &search);
    while (entPtr != NULL) {
	pollPtr = (Poll *) Ns_CacheGetValue(entPtr);
	if (pollPtr->dirty) {
	    NpsFlushEntry(pollPtr);
	}
	entPtr = Ns_CacheNextEntry(&search);
    }
    gdbm_sync(sdPtr->db);
}


static void
NpsFlusher(void *arg)
{
    SData *sdPtr = arg;

    Ns_CacheLock(sdPtr->cache);
    NpsFlushVotes(sdPtr);
    Ns_CacheUnlock(sdPtr->cache);
}


static void
NpsVote(SData *sdPtr, char *board, int choice, Votes *votesPtr)
{
    Poll *pollPtr;
    int new;
    Ns_Entry *entPtr;
    datum key, content;

    Ns_CacheLock(sdPtr->cache);
    entPtr = Ns_CacheCreateEntry(sdPtr->cache, board, &new);
    if (!new) {
	pollPtr = Ns_CacheGetValue(entPtr);
    } else {
	pollPtr = ns_calloc(1, sizeof(Poll));
	pollPtr->sdPtr = sdPtr;
	pollPtr->name = Ns_CacheKey(entPtr);
	key.dptr = pollPtr->name;
	key.dsize = strlen(key.dptr);
	content = gdbm_fetch(sdPtr->db, key);
	if (content.dptr != NULL) {
	    if (content.dsize > sizeof(Votes)) {
		content.dsize = sizeof(Votes);
	    }
	    memcpy(&pollPtr->votes, content.dptr, (size_t)content.dsize);
	    gdbm_free(content.dptr);
	}
	Ns_CacheSetValueSz(entPtr, pollPtr, sizeof(Poll));
    }
    if (choice >= 0 && choice < POLL_MAXCHOICE) {
	++pollPtr->votes.counts[choice];
	pollPtr->dirty = 1;
    }
    memcpy(votesPtr, &pollPtr->votes, sizeof(Votes));
    Ns_CacheUnlock(sdPtr->cache);
}    


static void
Nps2Vote(SData *sdPtr, char *board, int choice, int adjVoteBy, Votes *votesPtr)
{
    Poll *pollPtr;
    int new;
    Ns_Entry *entPtr;
    datum key, content;

    Ns_CacheLock(sdPtr->cache);
    entPtr = Ns_CacheCreateEntry(sdPtr->cache, board, &new);
    if (!new) {
	pollPtr = Ns_CacheGetValue(entPtr);
    } else {
	pollPtr = ns_calloc(1, sizeof(Poll));
	pollPtr->sdPtr = sdPtr;
	pollPtr->name = Ns_CacheKey(entPtr);
	key.dptr = pollPtr->name;
	key.dsize = strlen(key.dptr);
	content = gdbm_fetch(sdPtr->db, key);
	if (content.dptr != NULL) {
	    if (content.dsize > sizeof(Votes)) {
		content.dsize = sizeof(Votes);
	    }
	    memcpy(&pollPtr->votes, content.dptr, (size_t)content.dsize);
	    gdbm_free(content.dptr);
	}
	Ns_CacheSetValueSz(entPtr, pollPtr, sizeof(Poll));
    }
    if (choice >= 0 && choice < POLL_MAXCHOICE) {
        /*
         * Add new logic to adjust based on passed in value
         * If adjust by is a negative number and greater than
         * total, set total to 0.
         */
        if (adjVoteBy < 0 && abs(adjVoteBy) > pollPtr->votes.counts[choice]) {
           pollPtr->votes.counts[choice] = 0;
        } else {
           pollPtr->votes.counts[choice] += adjVoteBy;
        }
        pollPtr->dirty = 1;
    }
    memcpy(votesPtr, &pollPtr->votes, sizeof(Votes));
    Ns_CacheUnlock(sdPtr->cache);
}    


static int
NpsDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
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
NpsFindCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    datum key;
    char board[POLL_MAXNAME+1];
    char *p;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?pattern?\"", NULL);
	return TCL_ERROR;
    }
    Ns_CacheLock(sdPtr->cache);
    key = gdbm_firstkey(sdPtr->db);
    while (key.dptr) {
	if (key.dsize <= POLL_MAXNAME) {
	    strncpy(board, key.dptr, (size_t)key.dsize);
	    board[key.dsize] = '\0';
	    if (argc == 1 || Tcl_StringMatch(board, argv[1])) {
		Tcl_AppendElement(interp, board);
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
NpsVoteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    int choice;
    Votes votes;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " poll ?choice?\"", NULL);
	return TCL_ERROR;
    }
    if (strlen(argv[1]) >= POLL_MAXNAME) {
    	Tcl_AppendResult(interp, "invalid poll: ", argv[1], NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
	choice = -1;
    } else if (Tcl_GetInt(interp, argv[2], &choice) != TCL_OK) {
    	return TCL_ERROR;
    } 
    NpsVote(sdPtr, argv[1], choice, &votes);
    NpResult(interp, &votes);
    return TCL_OK;
}


static int
Nps2VoteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    SData *sdPtr = arg;
    int choice, adjVoteBy;
    Votes votes;

    if (argc < 2 || argc > 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " poll ?choice? ?adjust?\"", NULL);
	return TCL_ERROR;
    }
    if (strlen(argv[1]) >= POLL_MAXNAME) {
    	Tcl_AppendResult(interp, "invalid poll: ", argv[1], NULL);
	return TCL_ERROR;
    }

    /* Check for adjustment */

    if (argc == 4) {
        if (Tcl_GetInt(interp, argv[3], &adjVoteBy) != TCL_OK) {
            return TCL_ERROR;
        }
    } else {
        adjVoteBy = 1;
    }

    if (argc == 2) {
	/*
         * when choice is -1, we are interested in
         * obtaining counts, not setting them.
         */
	choice = -1;
    } else if (Tcl_GetInt(interp, argv[2], &choice) != TCL_OK) {
    	return TCL_ERROR;
    } 
    Nps2Vote(sdPtr, argv[1], choice, adjVoteBy, &votes);
    NpResult(interp, &votes);
    return TCL_OK;
}


static int
NpsDatabaseCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
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
NpsBackupCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
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
    NpsFlushVotes(sdPtr);
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
NpResult(Tcl_Interp *interp, Votes *votesPtr)
{
    unsigned int total, i;
    char buf[15];

    total = 0;
    for (i = 0; i < POLL_MAXCHOICE; ++i) {
	total += GetVote(votesPtr, i);
    }
    sprintf(buf, "%d", total);
    Tcl_AppendResult(interp, buf, NULL);
    for (i = 0; i < POLL_MAXCHOICE; ++i) {
	sprintf(buf, " %d", GetVote(votesPtr, i));
	Tcl_AppendResult(interp, buf, NULL);
    }
}
