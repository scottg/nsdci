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

static Tcl_CmdProc NcfSendCmd;
static Tcl_CmdProc NcfSendAllCmd;
static Tcl_CmdProc NcfSetFlushCmd;
static Ns_TclInterpInitProc AddCmds;
static Dci_RecvProc NcfRecv;
static Dci_RecvProc NcfRepeat;
static int fDebug;
static int flushOnInit;
static Dci_Broadcaster *broadcaster;
static char *login = "ncf";
static int NcfSend(char *cache, char *key, char *flag);
static void NcfParseMsg(Ns_DString *dsPtr, char **name, char **key, char **flag);
static void NcfFlushAll();
static void NcfFlushCache(char *name);
static Dci_Msg *NcfClientInitMsg(void *clientData);


/* Optional flag on cache flush message following key */
#define NCF_NONE ""
#define NCF_ALLKEYS "AC"   /* delete all keys of the given cache */
#define NCF_ALLCACHE "AK"  /* delete all keys of all caches that were configured */

/* Used for list of caches registered for automatic flush */
typedef struct FlushEnt {
    char name[DCI_RPCNAMESIZE];
    struct FlushEnt *nextPtr;
} FlushEnt;

static FlushEnt *flushList = NULL;

int
DciNcfInit(char *server, char *module)
{
    char *path;
    int client;
    int repeater;
    Ns_Set *set;

    Dci_LogIdent(module, rcsid);
    path = Ns_ConfigGetPath(server, module, "ncf", NULL);
    if (!Ns_ConfigGetBool(path, "debug", &fDebug)) {
	fDebug = 0;
    }
    if (!Ns_ConfigGetBool(path, "client", &client)) {
	client = 0;
    }
    if (!Ns_ConfigGetBool(path, "repeater", &repeater)) {
	repeater = 0;
    }
    if (!Ns_ConfigGetBool(path, "flushonconnect", &flushOnInit)) {
	flushOnInit = 0;
    }

    if (repeater) {
        Ns_Log(Notice, "ncf: creating repeater");
        if (Dci_CreateReceiver(login, NcfRepeat, NULL) != NS_OK) {
            Ns_Log(Error, "ncf: couldn't create repeater");
	    return NS_ERROR;
        }
    } else if (client) {
        Ns_Log(Notice, "ncf: creating client");
        if (Dci_CreateReceiver(login, NcfRecv, NULL) != NS_OK) {
            Ns_Log(Error, "ncf: couldn't create repeater");
	    return NS_ERROR;
        }
    }

    path = Ns_ConfigGetPath(server, module, "ncf/clients", NULL);
    set = Ns_ConfigGetSection(path);
    if (set != NULL) {
    	broadcaster = Dci_CreateBroadcaster(login, NULL, set, NcfClientInitMsg);
	if (broadcaster == NULL) {
	    return NS_ERROR;
	}
    }


    Ns_TclInitInterps(server, AddCmds, NULL);

    return NS_OK;
}


int
Dci_NcfSend(char *cache, char *key) {
    char *flag;

    if (key == NULL) {
        /* Null key pointer indicates that the entire cache should be flushed. */
        if (fDebug) {
            Ns_Log(Notice, "ncf: sending flush of entire cache %s", cache);
        }        
        key = NCF_NONE;
        flag = NCF_ALLKEYS;
    } else {
        flag = NCF_NONE;
    }
    return NcfSend(cache, key, flag);
}

static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "ncf.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "ncf.addFlushCache", NcfSetFlushCmd, NULL, NULL);
    if (broadcaster != NULL) {
        Tcl_CreateCommand(interp, "ncf.flushOnConnect", DciSetDebugCmd, &flushOnInit, NULL);
    	Tcl_CreateCommand(interp, "ncf.send", NcfSendCmd, NULL, NULL);
    	Tcl_CreateCommand(interp, "ncf.sendAll", NcfSendAllCmd, NULL, NULL);
    }
    return TCL_OK;
}

static int
NcfSendCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache key\"", NULL);
	return TCL_ERROR;
    }
    if (Dci_NcfSend(argv[1], argv[2]) != NS_OK) {
    	Tcl_AppendResult(interp, "could not flush: ", argv[1], ":", argv[2], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
NcfSendAllCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache\"", NULL);
	return TCL_ERROR;
    }
    if (Dci_NcfSend(argv[1], NULL) != NS_OK) {
    	Tcl_AppendResult(interp, "could not flush: ", argv[1], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}
/* Register a cache name for automatic flush when this machine receives
   an all-flush command */
static int
NcfSetFlushCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    FlushEnt *flushent;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache\"", NULL);
	return TCL_ERROR;
    }

    if (fDebug) {
        Ns_Log(Notice, "ncf: adding cache %s to list for automatic flush on connect", argv[1]);
    }
    
    flushent = ns_malloc(sizeof(FlushEnt));
    snprintf(flushent->name,sizeof(flushent->name),"%s",argv[1]);
    flushent->nextPtr = flushList;
    flushList = flushent;

    return TCL_OK;
}


/* Prepare the init message which gets sent by the broadcaster when connecting
 * to the client. We will use this message to optionally force a full cache flush
 * (of those caches which were registered on the client with ncf.addFlushCache)
 * This is needed primarily for repeaters, since during the time of disconnect, we
 * may have missed relaying any number of cache flush messages.
 */

static Dci_Msg *
NcfClientInitMsg(void *arg)
{

    Dci_Msg *msg;
    size_t clen, klen, flen;
    char *data;


    if (flushOnInit) {
        clen = 1;
        klen = 1; 
        flen = sizeof(NCF_ALLCACHE);

        msg = Dci_MsgAlloc(clen + klen + flen);
        data = Dci_MsgData(msg);
        memcpy(data, "", clen);
        memcpy(data + clen, "", klen);
        memcpy(data + clen + klen, NCF_ALLCACHE, flen);

        return msg;
    } else {
        return NULL;
    }
}

/* Flush all caches that were registered with ncf.addFlushCache */
static void
NcfFlushAll()
{
    FlushEnt *flushPtr;

    flushPtr = flushList;
    while (flushPtr != NULL) {
        NcfFlushCache(flushPtr->name);
        flushPtr = flushPtr->nextPtr;
    }

}

/* Flush all keys of the specified cache */
static void
NcfFlushCache(char *name)
{
    Ns_Cache *cache;

    cache = Ns_CacheFind(name);
    if (cache != NULL) {
        Ns_CacheLock(cache);
        if (fDebug) {
            Ns_Log(Notice, "ncf: performing full cache flush of %s",  name);
        }
        Ns_CacheFlush(cache);
        Ns_CacheBroadcast(cache);
        Ns_CacheUnlock(cache);
    } else {
        if (fDebug) {
            Ns_Log(Error, "ncf: could not find cache %s for flush", name);
        }
    }
}

/* Set pointers to name, key, and flag in the cache flush message */
static void
NcfParseMsg(Ns_DString *dsPtr, char **name, char **key, char **flag)
{
    unsigned int keylen;

    *name = dsPtr->string;
    *key  = *name + strlen(*name) + 1;
    keylen = dsPtr->length - (strlen(*name) + 1);  

    /* flag is optional (compatibility with messages from previous version) */    
    if (keylen > ( strlen(*key) + 1 ) ) {
        *flag = *key + strlen(*key) + 1;
    } else {
        *flag = (char *) &NCF_NONE;
    }
}

static int
NcfRecv(void *ignored, Ns_DString *dsPtr)
{
    Ns_Cache *cache;
    Ns_Entry *entry;
    char *name, *key, *flag;
  
    NcfParseMsg(dsPtr, &name, &key, &flag);

    /* Fast check to see if these are full cache flush messages */
    if (*key == '\0') {
        if (strcmp (flag,NCF_ALLCACHE) == 0) {
            if (fDebug) {
                Ns_Log(Notice, "ncf: received flush registered caches");
            }
            NcfFlushAll();
        } else {
             if (strcmp(flag,NCF_ALLKEYS) == 0) {
                 if (fDebug) {
                     Ns_Log(Notice, "ncf: received flush all keys of cache %s", name);
                 }   
                 NcfFlushCache(name);
                 cache = Ns_CacheFind(dsPtr->string);
             }
        }
    } else {
        if (fDebug) {
            Ns_Log(Notice, "ncf: received flush %s[%s]", name, key);
        }   
        cache = Ns_CacheFind(dsPtr->string);
        if (cache != NULL) {
            Ns_CacheLock(cache);
            entry = Ns_CacheFindEntry(cache, key);
            if (entry != NULL) {
                Ns_CacheFlushEntry(entry);
                Ns_CacheBroadcast(cache);

                if (fDebug) {
                    Ns_Log(Notice, "ncf: flushed: %s[%s]", name, key);
                }
            } else if (fDebug) {
                Ns_Log(Notice, "ncf: no such entry: %s[%s]", name, key);
            }

            Ns_CacheUnlock(cache);
        }
    }
    return NS_OK;
}

static int
NcfRepeat(void *ignored, Ns_DString *dsPtr)
{
    char *name, *key, *flag;

    NcfParseMsg(dsPtr, &name, &key, &flag);
    if (NcfSend(name, key, flag) != NS_OK) {
         Ns_Log(Error, "ncf: could not repeat: %s[%s]", name, key);
         return NS_ERROR;
    } 
    if (fDebug) {
        Ns_Log(Notice, "ncf: repeated: %s[%s]", name, key);
    }
    
    return NS_OK;
}


static int  
NcfSend(char *cache, char *key, char *flag)
{
    Dci_Msg *msg;
    size_t clen, klen, flen;
    char *data;

    if (broadcaster == NULL) {
    	Ns_Log(Error, "ncf: broadcaster not enabled");
	return TCL_ERROR;
    }
    clen = strlen(cache) + 1;
    klen = strlen(key) + 1; 
    flen = strlen(flag) + 1;

    msg = Dci_MsgAlloc(clen + klen + flen);
    data = Dci_MsgData(msg);
    memcpy(data, cache, clen);
    memcpy(data + clen, key, klen);
    memcpy(data + clen + klen, flag, flen);
    Dci_Broadcast(broadcaster, msg);
    if (fDebug) {
	Ns_Log(Notice, "ncf: queue for send: %s[%s]", cache, key);
    }
    return NS_OK;
}



