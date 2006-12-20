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

typedef struct List {
    Ns_Mutex    lock;
    Dci_List	list;
} List;

#define Key(lp,i)	(Dci_ListKey(&((lp)->list),(i)))
#define Value(lp,i)	(Dci_ListValue(&((lp)->list),(i)))

static Tcl_HashTable lists;
static int fDebug;

static int GetList(Tcl_Interp *interp, char *list, List **listPtrPtr);
static Tcl_CmdProc GetCmd;
static Tcl_CmdProc DumpCmd;
static Tcl_CmdProc ListsCmd;
static Tcl_CmdProc SizeCmd;
static Tcl_CmdProc IndexCmd;
static Tcl_CmdProc LoadCmd;


void
DciSlLibInit(void)
{
    DciAddIdent(rcsid);
    Tcl_InitHashTable(&lists, TCL_STRING_KEYS);
}


int
DciSlTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "sl.debug", DciSetDebugCmd, &fDebug, NULL);
    Tcl_CreateCommand(interp, "sl.load", LoadCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "sl.dump", DumpCmd, (ClientData) 'd', NULL);
    Tcl_CreateCommand(interp, "sl.names", DumpCmd, (ClientData) 'n', NULL);
    Tcl_CreateCommand(interp, "sl.lists", ListsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "sl.size", SizeCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "sl.get", GetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "sl.key", IndexCmd, (ClientData) 'k', NULL);
    Tcl_CreateCommand(interp, "sl.value", IndexCmd, (ClientData) 'v', NULL);
    return NS_OK;
}


static int
ListsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *key, *pattern;
    
    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    pattern = argv[1];
    hPtr = Tcl_FirstHashEntry(&lists, &search);
    while (hPtr != NULL) {
	key = Tcl_GetHashKey(&lists, hPtr);
	if (pattern == NULL || Tcl_StringMatch(key, argv[2])) {
            Tcl_AppendElement(interp, key);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    return TCL_OK;
}


static int
GetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    List *listPtr;
    Dci_Elem *elemPtr;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " list key\"", NULL);
        return TCL_ERROR;
    }
    if (GetList(interp, argv[1], &listPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&listPtr->lock);
    elemPtr = Dci_ListSearch(&listPtr->list, argv[2]);
    if (elemPtr != NULL) {
        Tcl_SetResult(interp, elemPtr->value, TCL_VOLATILE);
    }
    Ns_MutexUnlock(&listPtr->lock);
    return TCL_OK;
}


static int
IndexCmd(ClientData cmd, Tcl_Interp *interp, int argc, char **argv)
{
    List *listPtr;
    int i;
    
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " list index\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &i) != TCL_OK) {
	return TCL_ERROR;
    }
    if (i < 0) {
invalid:
	Tcl_AppendResult(interp, "invalid index: ", argv[2], NULL);
	return TCL_ERROR;
    }
    if (GetList(interp, argv[1], &listPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&listPtr->lock);
    if (i >= listPtr->list.nelem) {
	i = -1;
    } else {
	if ((int) cmd == 'k') {
	    Tcl_SetResult(interp, Key(listPtr, i), TCL_VOLATILE);
	} else {
	    Tcl_SetResult(interp, Value(listPtr, i), TCL_VOLATILE);
	}
    }
    Ns_MutexUnlock(&listPtr->lock);
    if (i < 0) {
	goto invalid;
    }
    return TCL_OK;
}


static int
SizeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    List *listPtr;
    int n;
    char buf[100];
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " list\"", NULL);
        return TCL_ERROR;
    }
    if (GetList(interp, argv[1], &listPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&listPtr->lock);
    n = listPtr->list.nelem;
    Ns_MutexUnlock(&listPtr->lock);
    sprintf(buf, "%d", n);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
DumpCmd(ClientData cmd, Tcl_Interp *interp, int argc, char **argv)
{
    List *listPtr;
    
    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " list ?pattern?\"", NULL);
        return TCL_ERROR;
    }
    if (GetList(interp, argv[1], &listPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&listPtr->lock);
    Dci_ListDump(interp, &listPtr->list, argv[2], ((int) cmd) == 'd' ? 1 : 0);
    Ns_MutexUnlock(&listPtr->lock);
    return TCL_OK;
}


static int
LoadCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int new;
    List *listPtr;
    Tcl_HashEntry *hPtr;
    Dci_List list;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " list values\"", NULL);
	return TCL_ERROR;
    }
    if (Dci_ListInit(&list, argv[2]) != TCL_OK) {
    	return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&lists, argv[1], &new);
    if (!new) {
	listPtr = Tcl_GetHashValue(hPtr);
    } else {
	/* NB: Only safe at single-threaded startup. */
	listPtr = ns_calloc(1, sizeof(List));
	Ns_MutexSetName2(&listPtr->lock, "dci:sl", argv[1]);
	Tcl_SetHashValue(hPtr, listPtr);
    }
    Ns_MutexLock(&listPtr->lock);
    if (listPtr->list.elems != NULL) {
	ckfree((char *) listPtr->list.elems);
    }
    listPtr->list = list;
    Ns_MutexUnlock(&listPtr->lock);
    if (fDebug) {
	Ns_Log(Notice, "sl[%s]: loaded, elems = %d ", argv[1], list.nelem);
    }
    return TCL_OK;
}


static int
GetList(Tcl_Interp *interp, char *list, List **listPtrPtr)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&lists, list);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such list: ", list, NULL);
	return TCL_ERROR;
    }
    *listPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}
