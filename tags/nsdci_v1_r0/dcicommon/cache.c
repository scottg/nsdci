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

static Tcl_CmdProc CreateCacheCmd;
static Tcl_CmdProc LockCmd;
static Tcl_CmdProc CreateEntryCmd;
static Tcl_CmdProc GetValueCmd;
static Tcl_CmdProc FindEntryCmd;
static Tcl_CmdProc UnsetValueCmd;
static Tcl_CmdProc DeleteEntryCmd;
static Tcl_CmdProc SetValueCmd;
static Tcl_CmdProc BroadcastCmd;
static Tcl_CmdProc SignalCmd;
static Tcl_CmdProc WaitCmd;
static Tcl_CmdProc TimedWaitCmd;
static Tcl_CmdProc FlushEntryCmd;

static int GetObj(Tcl_Interp *interp, int expectedPrefix, char *toid,
	Ns_Entry **entryPtr);
static char *MakeObj(int prefix, void *ptr, char *buf);


/*
 *----------------------------------------------------------------------
 *
 * DciCacheLibInit --
 *
 *      Initialization routine for the cache tcl module, called
 *      from init.c.
 *
 *      This module exposes nearly all the Ns_Cache C api's in Tcl.
 *      These api's function at a very low level and great care
 *      should be taken in their use. Please review the Ns_Cache
 *      section of the AOLserver documentation for more info.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *      Registers a tcl initialization function.
 *
 *----------------------------------------------------------------------
 */

void
DciCacheLibInit(void)
{
    DciAddIdent(rcsid);
}


/*
 *----------------------------------------------------------------------
 *
 * DciCacheTclInit --
 *
 *      Creates the cache tcl API's.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *      See above.
 *
 *----------------------------------------------------------------------
 */

int
DciCacheTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "cache.createTimed", CreateCacheCmd, (ClientData) 't', NULL);
    Tcl_CreateCommand(interp, "cache.createSized", CreateCacheCmd, (ClientData) 's', NULL);
    Tcl_CreateCommand(interp, "cache.lock", LockCmd, (ClientData) 'l', NULL);
    Tcl_CreateCommand(interp, "cache.unlock", LockCmd, (ClientData) 'u', NULL);
    Tcl_CreateCommand(interp, "cache.createEntry", CreateEntryCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.getValue", GetValueCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.findEntry", FindEntryCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.unsetValue", UnsetValueCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.deleteEntry", DeleteEntryCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.setValue", SetValueCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.broadcast", BroadcastCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.wait", WaitCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.timedWait", TimedWaitCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.signal", SignalCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "cache.flushEntry", FlushEntryCmd, NULL, NULL);
    
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetObj --
 *
 *      Retrieves object pointer from a string representation.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      Updates ptr with address from object.
 *
 *----------------------------------------------------------------------
 */
static int
GetObj(Tcl_Interp *interp, int expectedPrefix, char *toid, Ns_Entry **entryPtr)
{
    register char  *p = toid;
    void *ptr;

    ++p;
    if (*p++ == 'i' && *p++ == 'd'
        && sscanf(p, "%p", &ptr) == 1
        && ptr != NULL) {
	*entryPtr = ptr;
        return TCL_OK;
    } 
    if (interp != NULL) {
        Tcl_AppendResult(interp, "invalid object id \"",
                     toid, "\"", NULL);
    } 
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *    
 * MakeObj --
 *    
 *      Builds a string key from a pointer and prefix.
 *    
 * Results:
 *      Pointer to key.
 *    
 * Side effects:
 *      User supplied buffer is written to.
 *    
 *----------------------------------------------------------------------
 */   
static char *
MakeObj(int prefix, void *ptr, char *buf)
{     
    sprintf(buf, "%cid%p", prefix, ptr);
    return buf;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCache --
 *
 *      Retrieves a named cache.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Updates cachePtrPtr with NULL on failure, the address of
 *      the Ns_Cache structure on success.
 *
 *----------------------------------------------------------------------
 */
static int
GetCache(Tcl_Interp *interp, char *name, Ns_Cache **cachePtrPtr)
{
    Ns_Cache *cachePtr;
    
    *cachePtrPtr = NULL;
    cachePtr = Ns_CacheFind(name);
    if (cachePtr == NULL) {
        Tcl_AppendResult(interp, "no such cache: ", name, NULL);
        return TCL_ERROR;
    }
    *cachePtrPtr = cachePtr;
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateCacheCmd --
 *
 *      Creates a named cache, pruned either by size or time. Pruning
 *      method is determined by the ClientData argument to this
 *      function ('t' for timed, 's' for size).
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Cache will be created. Currently, the cache api allows 
 *      creation of multiple caches of the same name, essentialy
 *      overwritting the previous cache causing a memory leak.
 *
 *----------------------------------------------------------------------
 */
static int
CreateCacheCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int type = (int) arg;
    int optVal;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName ",  (type == 't' ? "timeout" : "size"), "\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &optVal) != TCL_OK) {
        return TCL_ERROR;
    }
    
    /*
     * CacheCreate never fails, will overwrite and existing cache
     * if named cache already exists.
     */
     
    if (type == 't') {
        Ns_CacheCreate(argv[1], TCL_STRING_KEYS, optVal, ns_free);
    } else {
        Ns_CacheCreateSz(argv[1], TCL_STRING_KEYS, (size_t)optVal, ns_free);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LockCmd --
 *
 *      Provides support for both locking and unlocking the cache,
 *      based on the ClientData ('l' for lock, 'u' for unlock).
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *      Cache's mutex is locked/unlocked.
 *
 *----------------------------------------------------------------------
 */
static int
LockCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int type = (int) arg;
    Ns_Cache *cachePtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName\"", NULL);
	return TCL_ERROR;
    }    
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    if (type == 'l') {
        Ns_CacheLock(cachePtr);
    } else {
        Ns_CacheUnlock(cachePtr);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateEntryCmd --
 *
 *      Creates an entry in a named cache identified by "key". 
 *      "newVariable" is updated with 1 if an entry for the given key
 *      already exists else 0. A handle to the entry is returned.
 *
 *      NB: The remaining entry-based api's do not know if the entry
 *      handle returned by this function is still valid, and since the
 *      address of the entry is sprintf'd into the handle itself,
 *      dereferencing a bogus handle could crash the server. Therefore,
 *      the cache should be locked when using these api's.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      An entry is created in the cache if one does not already 
 *      exist, and "newVariable" is updated as described above.
 *
 *----------------------------------------------------------------------
 */
static int
CreateEntryCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    Ns_Entry *entryPtr;
    int new;
    char buf[24];
    
    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName key newVariable\"", NULL);
	return TCL_ERROR;
    }    
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    entryPtr = Ns_CacheCreateEntry(cachePtr, argv[2], &new);
    if (Tcl_SetVar(interp, argv[3], (new ? "1" : "0"), TCL_LEAVE_ERR_MSG) == NULL) {
        return TCL_ERROR;
    }
    if (entryPtr != NULL) {
        MakeObj('e', entryPtr, buf);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else {
        Tcl_SetResult(interp, "", TCL_STATIC);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetValueCmd --
 *
 *      Retrieves the string value of an entry handle returned by
 *      CreateEntryCmd or FindEntryCmd. The dataVariable argument is
 *      then updated with the value if it is not NULL. If the value
 *      is not NULL a "1" is returned, else "0".
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
GetValueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entryPtr;
    char    *data;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " entry dataVariable\"", NULL);
	return TCL_ERROR;
    }
    if (GetObj(interp, 'e', argv[1], &entryPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    data = Ns_CacheGetValue(entryPtr);
    if (data != NULL) {
        if (Tcl_SetVar(interp, argv[2], data, TCL_LEAVE_ERR_MSG) == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetResult(interp, "1", TCL_STATIC);
    } else {
        Tcl_SetResult(interp, "0", TCL_STATIC);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FindEntryCmd --
 *
 *      Finds a cache entry identified by "key" in a named cache. A 
 *      handle to this entry is returned.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      See note in CreateEntryCmd.
 *
 *----------------------------------------------------------------------
 */
static int
FindEntryCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    Ns_Entry *entryPtr;
    char buf[24];
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName key\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    entryPtr = Ns_CacheFindEntry(cachePtr, argv[2]);
    if (entryPtr != NULL) {
        MakeObj('e', entryPtr, buf);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else {
        Tcl_SetResult(interp, "", TCL_STATIC);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UnsetValueCmd --
 *
 *      Unsets the string value of a given cache entry, freeing the
 *      previous data and resetting the value to NULL. This does not
 *      delete the entry itself.
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
UnsetValueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entryPtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " entry\"", NULL);
	return TCL_ERROR;
    }
    if (GetObj(interp, 'e', argv[1], &entryPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheUnsetValue(entryPtr);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DeleteEntryCmd --
 *
 *      Removes an entry from the named cache.
 *
 *      NOTE: This api does not free the memory associated with the
 *      cache entry's value. To delete an entry and free its current
 *      value, you must call FlushEntryCmd.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Entry is removed from the cache.
 *
 *----------------------------------------------------------------------
 */
static int
DeleteEntryCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entryPtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " entry\"", NULL);
	return TCL_ERROR;
    }
    if (GetObj(interp, 'e', argv[1], &entryPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheDeleteEntry(entryPtr);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SetValueCmd --
 *
 *      Updates the cache entry's value, freeing the existing value if
 *      not NULL.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      "value" is copied, existing entry value may be free'd.
 *
 *----------------------------------------------------------------------
 */
static int
SetValueCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entryPtr;
    char *dataPtr;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " entry value\"", NULL);
	return TCL_ERROR;
    }
    if (GetObj(interp, 'e', argv[1], &entryPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    dataPtr = ns_strdup(argv[2]);
    Ns_CacheSetValueSz(entryPtr, dataPtr, strlen(dataPtr));
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * BroadcastCmd --
 *
 *      Broadcast the cache's condition variable, waking all waiting
 *  	threads (if any).
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Threads may resume.
 *
 *----------------------------------------------------------------------
 */
static int
BroadcastCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheBroadcast(cachePtr);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SignalCmd --
 *
 *      Signal the cache's condition variable, waking the first waiting
 *  	thread (if any).
 *
 *  	NOTE:  Be sure you don't really want to wake all threads with
 *  	Ns_CacheBroadcast.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      A single thread may resume.
 *
 *----------------------------------------------------------------------
 */
static int
SignalCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheSignal(cachePtr);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * WaitCmd --
 *
 *      Wait indefinitely for the cache's condition variable to be
 *  	signaled.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Thread is suspended until condition is signaled.
 *
 *----------------------------------------------------------------------
 */
static int
WaitCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheWait(cachePtr);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TimedWaitCmd --
 *
 *      Wait for the cache's condition variable to be
 *  	signaled or the given absolute timeout in seconds.
 *
 * Results:
 *      Standard Tcl result code.
 *
 * Side effects:
 *      Thread is suspended until condition is signaled or timeout.
 *
 *----------------------------------------------------------------------
 */
static int
TimedWaitCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cachePtr;
    Ns_Time timeout;
    int seconds;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " cacheName seconds\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &seconds) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, seconds, 0);
    
    Ns_CacheTimedWait(cachePtr, &timeout);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FlushEntryCmd --
 *
 *      Delete an entry from the cache table after first unsetting
 *  	the current entry value (if any).
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
FlushEntryCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entryPtr;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " entry\"", NULL);
	return TCL_ERROR;
    }
    if (GetObj(interp, 'e', argv[1], &entryPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Ns_CacheFlushEntry(entryPtr);
    
    return TCL_OK;
}
