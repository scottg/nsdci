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
    char *content;
    int length;
    int flags;
} Post;

typedef struct {
    char *splitbuf;
    int  npost;
    Post post[1];
} Board;

static int CbGetId(char *post);


/*
 *----------------------------------------------------------------------
 *
 * DciCbApiLibInit --
 *
 *      Init the CB API.
 *
 * Results:
 *      Standard AOLserver return code.
 *
 * Side effects:
 *      Nothing.
 *
 *----------------------------------------------------------------------
 */

void
DciCbApiLibInit(void)
{
    DciAddIdent(rcsid);
}    
 

/*
 *----------------------------------------------------------------------
 *
 * Dci_CbParse --
 *
 *      Parse a string into an internal, reversed list.
 *
 * Results:
 *      Pointer to board object or NULL on error.
 *
 * Side effects:
 *      Nothing.
 *
 *----------------------------------------------------------------------
 */

Dci_Board *
Dci_CbParse(char *msgs)
{
    Board *boardPtr;
    int i, argc;
    char **argv;

    if (Tcl_SplitList(NULL, msgs, &argc, &argv) != TCL_OK) {
	return NULL;
    }

    /*
     * Otherwise, split, scan, and reverse NCB list result and
     * cast the resulting Board pointer to the string data pointer.
     */
		
    boardPtr = ns_malloc(sizeof(Board) + (sizeof(Post) * argc));
    boardPtr->splitbuf = (char *) argv;
    boardPtr->npost = argc;
    for (i = 0; --argc >= 0; ++i) {
	boardPtr->post[i].content = argv[argc];
	boardPtr->post[i].length =
		Tcl_ScanElement(boardPtr->post[i].content, &boardPtr->post[i].flags) + 1;
    }
    return (Dci_Board *) boardPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_CbGet --
 *
 *      Get a message list from a board.
 *
 * Results:
 *      Pointer to ns_malloc'ed string with message list.
 *
 * Side effects:
 *      Nothing.
 *
 *----------------------------------------------------------------------
 */

char *
Dci_CbGet(Dci_Board *board, int first, int last, int *npostsPtr)
{
    Board *boardPtr = (Board *) board;
    int nposts, i;
    size_t len;
    char *dst, *msgs;

    nposts = boardPtr->npost;
    if (npostsPtr != NULL) {
	*npostsPtr = nposts;
    }
    if (first < 0) {
	first = 0;
    } else if (first > nposts) {
	first = nposts;
    }
    if (last < 0) {
	last = 0;
    } else if (last > nposts) {
	last = nposts;
    }
    if (last < first) {
	last = first;
    }
    len = 1;
    for (i = first; i < last; ++i) {
       	len += boardPtr->post[i].length;
    }
    msgs = ns_malloc(len);
    dst = msgs;
    for (i = first; i < last; ++i) {
        len = Tcl_ConvertElement(boardPtr->post[i].content, dst, boardPtr->post[i].flags);
        dst += len;
        *dst = ' ';
        dst++;
    }
    if (dst == msgs) {
        *dst = 0;
    } else {
        dst[-1] = 0;
    }
    return msgs;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_CbPost --
 *
 *      Append a formatted, time-stamped post to a message string.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *	The given Ns_DString is modified in place with old posts
 *	beyond the given maxposts dumped.
 *
 *----------------------------------------------------------------------
 */

int
Dci_CbPost(char *board, Ns_DString *dsPtr, char *user, char *msg, char *fields, int maxposts)
{
    int argc, id, i, start;
    char **argv, buf[20];
    Ns_DString post;

    if (Tcl_SplitList(NULL, dsPtr->string, &argc, &argv) != TCL_OK) {
	return NS_ERROR;
    }
    if (argc == 0) {
	id = 0;
    } else {
	id = CbGetId(argv[argc-1]);
	if (id < 0) {
	    ckfree((char *) argv);
	    return NS_ERROR;
	}
	++id;
    }
    Ns_DStringInit(&post);
    sprintf(buf, "%d", id);
    Ns_DStringAppendElement(&post, buf);
    sprintf(buf, "%d", (int) time(NULL));
    Ns_DStringAppendElement(&post, buf);
    Ns_DStringAppendElement(&post, user);
    Ns_DStringAppendElement(&post, msg);
    Ns_DStringAppendElement(&post, fields ? fields : "");
    if (argc >= maxposts) {
	Ns_DStringTrunc(dsPtr, 0);
	start = argc - maxposts + 1;
	for (i = start; i < argc; ++i) {
    	    Ns_DStringAppendElement(dsPtr, argv[i]);
    	    Ns_DStringNAppend(dsPtr, "\n", 1);
	}
    }
    while (dsPtr->length > 0 && dsPtr->string[dsPtr->length-1] == '\n') {
	Ns_DStringTrunc(dsPtr, dsPtr->length - 1);
    }
    if (dsPtr->length > 0) {
        Ns_DStringNAppend(dsPtr, "\n", 1);
    }
    Ns_DStringAppendElement(dsPtr, post.string);
    Ns_DStringNAppend(dsPtr, "\n", 1);
    Ns_DStringFree(&post);
    ckfree((char *) argv);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_CbDelete --
 *
 *      Delete a post from a board.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *	The given Ns_DString is modified in place without the first
 *	post with the given id.
 *
 *----------------------------------------------------------------------
 */

int
Dci_CbDelete(char *board, Ns_DString *dsPtr, int id)
{
    int argc, found, i, n, status;
    char **argv;

    if (Tcl_SplitList(NULL, dsPtr->string, &argc, &argv) != TCL_OK) {
	return NS_ERROR;
    }
    Ns_DStringTrunc(dsPtr, 0);
    status = NS_ERROR;
    found = 0;
    for (i = 0; i < argc; ++i) {
	if (!found) {
	    n = CbGetId(argv[i]);
	    if (n < 0) {
		goto done;
	    }
	    if (n == id) {
	    	found = 1;
	    	continue;
	    }
	}
	Ns_DStringAppendElement(dsPtr, argv[i]);
	Ns_DStringNAppend(dsPtr, "\n", 1);
    }
    status = NS_OK;
done:
    ckfree((char *) argv);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Dci_CbFree --
 *
 *      Free a parsed board.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Dci_CbFree(Dci_Board *board)
{
    Board *boardPtr = (Board *) board;

    ckfree((char *) boardPtr->splitbuf);
    ns_free(boardPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * CbGetId --
 *
 *      Utility routine to extract the message id from a post.
 *
 * Results:
 *      Message id or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CbGetId(char *post)
{
    int argc, id;
    char **argv;

    id = -1;
    if (Tcl_SplitList(NULL, post, &argc, &argv) == TCL_OK) {
	if (argc == 5 && Tcl_GetInt(NULL, argv[0], &id) != TCL_OK) {
	    id = -1;
	}
	ckfree((char *) argv);
    }
    return id;
}
