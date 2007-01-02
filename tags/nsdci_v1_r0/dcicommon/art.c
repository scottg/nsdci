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

/*
 * The following structure is used to cache the width and
 * height strings for a piece of art.
 */
 
typedef struct {
    char width[10];
    char height[10];
} Dims;

static Ns_Callback ArtCheck;
static Tcl_CmdProc GetUrlCmd;
static Tcl_CmdProc FlushCmd;
static Tcl_CmdProc GetDimsCmd;
static Tcl_CmdProc GetTagCmd;
static Ns_TclInterpInitProc AddCmds;
static int GetKey(Tcl_Interp *interp, int argc, char **argv, char *path);
static int GetDims(Tcl_Interp *interp, char *key, char *wbuf, char *hbuf);
static void FetchDims(Tcl_Interp *interp, char *key, char *wbuf, char *hbuf);
static void AppendUrl(Tcl_Interp *interp, char *key);
#define MakeKey(b,g,n)	(sprintf((b), "%s/%s", (g), (n)))

static char *host;
static int port;
static int timeout;
static char portBuf[10];
static Ns_Cache *cache;


/*
 *----------------------------------------------------------------------
 *
 * DciArtInit --
 *
 *	Initialize the artserver client.
 *
 * Results:
 *	NS_OK.
 *
 * Side effects:
 *	Cache created, periodic check scheduled, commands added.
 *
 *----------------------------------------------------------------------
 */

int
DciArtInit(char *server, char *module)
{
    int i;
    char *path;

    Dci_LogIdent(module, rcsid);
    
    /*
     * The host and port configs are used both when fetching
     * the width and height and to output directly to the page.
     * The host should idealy be an IP address to avoid DNS
     * lookup in GetDims() and by the client browser.
     */

    path = Ns_ConfigGetPath(server, module, "art", NULL);
    if (path == NULL) {
	path = "dci/art";
    }
    host = Ns_ConfigGetValue(path, "host");
    if (host == NULL) {
	host = "127.0.0.1";
    }
    if (!Ns_ConfigGetInt(path, "port", &port)) {
	port = 80;
    }
    sprintf(portBuf, ":%d", port);
    
    /*
     * Create a cache for the art attributes.
     */
     
    if (!Ns_ConfigGetInt(path, "cachesize", &i) || i < 0) {
    	i = 2*1024*1024; /* 2megs */
    }
    i /= sizeof(Dims) + 1;
    cache = Ns_CacheCreateSz("art", TCL_STRING_KEYS, (size_t)i, ns_free);

    /*
     * Set the timeout in milliseconds, default 1000 (1 second).
     */

    if (!Ns_ConfigGetInt(path, "timeout", &i) || i < 1) {
	i = 1000;
    }
    timeout = i;

    /*
     * Schedule a procedure to purge unknown width/height entries.
     * This allows the client to periodically retry fetches if the
     * art server isn't responding (which would probably be a problem
     * for the users browser).
     */
         
    if (!Ns_ConfigGetInt(path, "checkinterval", &i) || i < 1) {
	i = 300; /* 5 minutes */
    }
    Ns_ScheduleProc(ArtCheck, NULL, 0, i);

    /*
     * Add the art.* commands.
     */
     
    Ns_TclInitInterps(server, AddCmds, NULL);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ArtGetDims --
 *
 *	Copy image width/height into given buffers.  Note: No check is
 *  	made of given buffers, caller must ensure sufficient space
 *  	to sprintf int values. 
 *
 * Results:
 *	See GetDims().
 *
 * Side effects:
 *	See GetDims().
 *
 *----------------------------------------------------------------------
 */

int
Dci_ArtGetDims(Tcl_Interp *interp, char *group, char *name, char *wbuf,
	       char *hbuf)
{
    char key[PATH_MAX];

    MakeKey(key, group, name);
    return GetDims(interp, key, wbuf, hbuf);
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_ArtGetUrl --
 *
 *	Copy image URL to given dstring.  
 *
 * Results:
 *	Pointer to given dstring value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Dci_ArtGetUrl(Ns_DString *dsPtr, char *group, char *name)
{
    char key[PATH_MAX];

    MakeKey(key, group, name);
    Ns_DStringVarAppend(dsPtr, "http://", host, NULL);
    if (port != 80) {
	Ns_DStringAppend(dsPtr, portBuf);
    }
    Ns_DStringVarAppend(dsPtr, "/", key, NULL);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * AddCmds --
 *
 *	Ns_TclInitInterps() callback to add commands.
 *
 * Results:
 *	TCL_OK.
 *
 * Side effects:
 *	Art commands added.
 *
 *----------------------------------------------------------------------
 */

static int
AddCmds(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "art.getTag", GetTagCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "art.getUrl", GetUrlCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "art.getXY", GetDimsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "art.getWidth", GetDimsCmd,
    	    	      (ClientData) "w", NULL);
    Tcl_CreateCommand(interp, "art.getHeight", GetDimsCmd,
    	    	      (ClientData) "h", NULL);
    Tcl_CreateCommand(interp, "art.flushXY", FlushCmd, NULL, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetTagCmd --
 *
 *  	Return an HTML IMG tag fragment for URL to an image with
 *  	width, height, and optionally alt text.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Dimensions of image may be fetched from artserver.
 *
 *----------------------------------------------------------------------
 */

static int
GetTagCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    char key[PATH_MAX];
    char wbuf[10], hbuf[10];

    /*
     * art.getTag <group name> <file name> ?<alt tag>? ?<attribute string>?
     */
    
    if (GetKey(interp, argc, argv, key) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Tcl_AppendResult(interp, "<img border=\"0\" src=\"", NULL);
    AppendUrl(interp, key);
    Tcl_AppendResult(interp, "\"", NULL);
    if (GetDims(interp, key, wbuf, hbuf)) {
        Tcl_AppendResult(interp, " width=\"", wbuf,
            "\" height=\"", hbuf, "\"", NULL);
    }

    /* Add alt tag */
    if (argc > 3) {
        /* TODO: need to escape " as &quot; here. */
        Tcl_AppendResult(interp, " alt=\"", argv[3], "\"", NULL);
    }

    /* Add any other attributes specified. */
    if (argc > 4)
        Tcl_AppendResult(interp, " ", argv[4], NULL);

    Tcl_AppendResult(interp, "/>", NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetUrlCmd --
 *
 *  	Return the URL to an image.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetUrlCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    char key[PATH_MAX];

    if (GetKey(interp, argc, argv, key) != TCL_OK) {
	return TCL_ERROR;
    }
    AppendUrl(interp, key);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetDimsCmd --
 *
 *  	Return width and/or height for an image.  This function handles
 *  	multiple commands specified by the arg clientdata.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Dimensions of image may be fetched from artserver.
 *
 *----------------------------------------------------------------------
 */

static int
GetDimsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    char key[PATH_MAX];
    char *cmd = (char *) arg;
    char wbuf[10], hbuf[10];

    if (GetKey(interp, argc, argv, key) != TCL_OK) {
	return TCL_ERROR;
    }
    if (GetDims(interp, key, wbuf, hbuf)) {
	if (argc == 5) {

    	    /*
	     * Set width and height in given variables.
	     */
	     	
	    if (Tcl_SetVar(interp, argv[3], wbuf, TCL_LEAVE_ERR_MSG) == NULL) {
		return TCL_ERROR;
	    }
	    if (Tcl_SetVar(interp, argv[4], hbuf, TCL_LEAVE_ERR_MSG) == NULL) {
		return TCL_ERROR;
	    }
	    Tcl_SetResult(interp, "1", TCL_STATIC);

	} else {

	    /*
	     * Return width and/or height directly.
	     */
		 
	    if (cmd == NULL) {
	    	Tcl_AppendElement(interp, wbuf);
		Tcl_AppendElement(interp, hbuf);
	    } else if (*cmd == 'w') {
	    	Tcl_SetResult(interp, wbuf, TCL_VOLATILE);
	    } else if (*cmd == 'h') {
	    	Tcl_SetResult(interp, hbuf, TCL_VOLATILE);
	    }
	}

    } else {
    	if (argc == 5) {

    	    /*
	     * Request was to update given variables, return 0 on
	     * failure instead of null result.
	     */

	    Tcl_SetResult(interp, "0", TCL_STATIC);
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FlushCmd --
 *
 *  	Flush an art cache entry.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Entry, if it exists, is flushed.
 *
 *----------------------------------------------------------------------
 */
 
static int
FlushCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Entry *entry;
    char key[PATH_MAX];

    if (GetKey(interp, argc, argv, key) != TCL_OK) {
	return TCL_ERROR;
    }

    Ns_CacheLock(cache);
    entry = Ns_CacheFindEntry(cache, key);
    if (entry != NULL) {
	Ns_CacheFlushEntry(entry);
    	Ns_CacheBroadcast(cache);
    }
    Ns_CacheUnlock(cache);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetKey --
 *
 *  	Verify art command args and construct a file key.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Error message will be left in interp->result on invalid args.
 *
 *----------------------------------------------------------------------
 */

static int
GetKey(Tcl_Interp *interp, int argc, char **argv, char *path)
{
    if (argc < 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " group name ?...?\"", NULL);
	return TCL_ERROR;
    }
    MakeKey(path, argv[1], argv[2]);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendUrl --
 *
 *  	Append the image URL to the configured artserver.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendUrl(Tcl_Interp *interp, char *key)
{
    Tcl_AppendResult(interp, "http://", host, NULL);
    if (port != 80) {
	Tcl_AppendResult(interp, portBuf, NULL);
    }
    Tcl_AppendResult(interp, "/", key, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * GetDims --
 *
 *  	Core routine to fetch width and height from cache.
 *
 * Results:
 *	1 or 0 if dimensions could be fetched.
 *
 * Side effects:
 *	May HTTP request the x-art headers from the artserver.
 *
 *----------------------------------------------------------------------
 */

static int
GetDims(Tcl_Interp *interp, char *key, char *wbuf, char *hbuf)
{
    Dims *dPtr;
    Ns_Entry *entry;
    int new;

    if (cache == NULL) {
	FetchDims(interp, key, wbuf, hbuf);
    } else {
	dPtr = NULL;
	Ns_CacheLock(cache);
	entry = Ns_CacheCreateEntry(cache, key, &new);
	if (!new) {
	    while (entry != NULL && (dPtr = Ns_CacheGetValue(entry)) == NULL) {
		Ns_CacheWait(cache);
		entry = Ns_CacheFindEntry(cache, key);
	    }
	}
	if (dPtr == NULL) {
	    Ns_CacheUnlock(cache);
	    dPtr = ns_malloc(sizeof(Dims));
	    FetchDims(interp, key, dPtr->width, dPtr->height);
	    Ns_CacheLock(cache);
	    entry = Ns_CacheCreateEntry(cache, key, &new);
	    Ns_CacheSetValueSz(entry, dPtr, 1);
	    Ns_CacheBroadcast(cache);
	}
	strcpy(wbuf, dPtr->width);
	strcpy(hbuf, dPtr->height);
	Ns_CacheUnlock(cache);
    }
    if (*wbuf && *hbuf) {
	return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * FetchDims --
 *
 *  	Fetch the dimensions of an image via a fast HTTP HEAD.  This
 *  	routine is called by GetDims() and broken out for readable
 *  	only.
 *
 * Results:
 *	Pointer to new Dims structure.
 *
 * Side effects:
 *  	May block up to a second on HTTP response.
 *
 *----------------------------------------------------------------------
 */

static void
FetchDims(Tcl_Interp *interp, char *key, char *wbuf, char *hbuf)
{
    Ns_Set *hdrs;
    Ns_DString ds;
    unsigned long w, h;
    char *whdr, *hhdr;

    Ns_DStringInit(&ds);
    Ns_DStringPrintf(&ds, "http://%s:%d/%s", host, port, key);
    hdrs = Ns_SetCreate(NULL);
    Ns_SetPut(hdrs, "Host", host);
    if (DciHttpGet(interp, "HEAD", ds.string, hdrs, timeout) == TCL_OK &&
	    (whdr = Ns_SetIGet(hdrs, "x-art-width")) != NULL &&
	    (hhdr = Ns_SetIGet(hdrs, "x-art-height")) != NULL &&
	    ((w = strtoul(whdr, NULL, 0)) > 0) &&
	    ((h = strtoul(hhdr, NULL, 0)) > 0)) {
	sprintf(wbuf, "%ul", w);
	sprintf(hbuf, "%ul", h);
    } else{
	*wbuf = *hbuf = '\0';
    }
    Ns_SetFree(hdrs);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * ArtCheck --
 *
 *  	Scheduled procedure to purge all invalid responses from the
 *  	cache so they may be re-tried when next requested.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	May purge entries.
 *
 *----------------------------------------------------------------------
 */

static void
ArtCheck(void *ignored)
{
    Ns_Entry *entry;
    Ns_CacheSearch search;
    Dims *dPtr;

    Ns_CacheLock(cache);
    entry = Ns_CacheFirstEntry(cache, &search);
    while (entry != NULL) {
	dPtr = Ns_CacheGetValue(entry);
	if (dPtr && (*dPtr->width == '\0' || *dPtr->height == '\0')) {
	    Ns_CacheFlushEntry(entry);
	}
	entry = Ns_CacheNextEntry(&search);
    }
    Ns_CacheBroadcast(cache);
    Ns_CacheUnlock(cache);
}
