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

#include "dciadmin.h"

int
Dci_GetLinksCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Set *setPtr;
    Tcl_DString ds, dsHrefAtts;
    char *buf, *blob, *name, *page, *key, *value;
    char *s, *e, *t;
    int i;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " blob\"", NULL);
	return TCL_ERROR;
    }

    buf = ns_strdup(argv[1]);
    blob = buf;
    Tcl_DStringInit(&ds);
    Tcl_DStringInit(&dsHrefAtts);
    setPtr = Ns_SetCreate("tagAtts");
    while ((s = strchr(blob, '<')) && (e = strchr(++s, '>'))) {
	*e++ = '\0';
	t = s;
	while (*s && !isspace(UCHAR(*s))) {
	    ++s;
	}
	if (*s) {
	    *s++ = '\0';
	}
	if (STREQ(t, "link")) {
            name = page = NULL;
            Ns_SetTrunc(setPtr, 0);
            Dci_FindTagAtts(setPtr, s, e - s);
            Tcl_DStringSetLength(&dsHrefAtts, 0);

            for (i = 0; setPtr != NULL && i < Ns_SetSize(setPtr); i++) {
                if (STREQ(Ns_SetKey(setPtr, i), "name")) {
                    name = Ns_SetValue(setPtr, i);
                } else if (STREQ(Ns_SetKey(setPtr, i), "page")) {
                    page = Ns_SetValue(setPtr, i);
                } else {
                    key = Ns_SetKey(setPtr, i);
                    value = Ns_SetValue(setPtr, i);
                    Tcl_DStringAppend(&dsHrefAtts, key, -1);
                    Tcl_DStringAppend(&dsHrefAtts, "=\"", 2);
                    Tcl_DStringAppend(&dsHrefAtts, value, -1);
                    Tcl_DStringAppend(&dsHrefAtts, "\" ", 2);
                }
            }
            Tcl_DStringStartSublist(&ds);
	    if (name) {
		Tcl_DStringAppendElement(&ds, name);
		Tcl_DStringAppendElement(&ds, page ? page : "");
	    } else {
		Tcl_DStringAppendElement(&ds, "");
		Tcl_DStringAppendElement(&ds, "");
            }
            Tcl_DStringAppendElement(&ds, dsHrefAtts.string);
            Tcl_DStringEndSublist(&ds);
	}
	blob = e;
    }
    ns_free(buf);
    Ns_SetFree(setPtr);
    Tcl_DStringFree(&dsHrefAtts);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}


typedef struct Link {
    char *link;
    char *page;
    char *hrefAtts;
    char  buf[1];
} Link;


#define PROMO_MAXLINK 127

static int
FindLink(Link **links, int *nlinksPtr, char *link, char *page, char *hrefAtts)
{
    int i, llen;
    Link *lPtr;
    
    for (i = 0; i < *nlinksPtr; ++i) {
    	lPtr = links[i];
	if (STREQ(lPtr->link, link) && STREQ(lPtr->page, page) &&
                STREQ(lPtr->hrefAtts, hrefAtts)) {
	    return i;
	}
    }
    if (i == PROMO_MAXLINK) {
    	return -1;
    }
    llen = strlen(link);
    lPtr = ns_malloc(sizeof(Link) + llen + strlen(page) + strlen(hrefAtts) + 2);
    lPtr->link = &lPtr->buf[0];
    lPtr->page = &lPtr->buf[llen+1];
    lPtr->hrefAtts = &lPtr->buf[llen+1+strlen(page)+1];
    strcpy(lPtr->link, link);
    strcpy(lPtr->page, page);
    strcpy(lPtr->hrefAtts, hrefAtts);
    links[i] = lPtr;
    *nlinksPtr += 1;
    return i;
}

static void
AppendText(Ns_DString *dsPtr, char *text)
{
    Ns_DStringNAppend(dsPtr, "t", 1);
    Ns_DStringAppendArg(dsPtr, text);
}    

int
Dci_WriteLinksCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;
    Ns_Set *setPtr;
    Tcl_DString tds, dsHrefAtts;
    char *buf, *link, *page, *key, *value;
    char *s, *e, *t, *p;
    char *deflink, *defpage;
    Link *links[PROMO_MAXLINK];
    int nlinks, index, result, len, i;
    char c;

    if (argc < 4 || argc > 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " chanId text linksVar ?defLink? ?defPage?\"", NULL);
	return TCL_ERROR;
    }
    deflink = defpage = "";
    if (argc > 4) {
    	deflink = argv[4];
	if (argc > 5) {
	    defpage = argv[5];
	}
    }
    nlinks = 0;
    (void) FindLink(links, &nlinks, deflink, defpage, "");
    buf = ns_strdup(argv[2]);

    Ns_DStringInit(&ds);
    Tcl_DStringInit(&dsHrefAtts);
    setPtr = Ns_SetCreate("tagAtts");
    p = buf;

    while ((s = strstr(p, "<link")) && (e = strstr(s+5, "</link>"))
    	    && (t = strchr(s+5, '>')) && t < e) {
    	*s = *e = *t = '\0';
	s += 5;
	e += 7;
	t += 1;
    	AppendText(&ds, p);
    	link = NULL;
	page = NULL;
        Ns_SetTrunc(setPtr, 0);
        Dci_FindTagAtts(setPtr, s, e - s);
        Tcl_DStringSetLength(&dsHrefAtts, 0);

        for (i = 0; setPtr != NULL && i < Ns_SetSize(setPtr); i++) {
            if (STREQ(Ns_SetKey(setPtr, i), "name")) {
                link = Ns_SetValue(setPtr, i);
            } else if (STREQ(Ns_SetKey(setPtr, i), "page")) {
                page = Ns_SetValue(setPtr, i);
            } else {
                key = Ns_SetKey(setPtr, i);
                value = Ns_SetValue(setPtr, i);
                Tcl_DStringAppend(&dsHrefAtts, key, -1);
                Tcl_DStringAppend(&dsHrefAtts, "=\"", 2);
                Tcl_DStringAppend(&dsHrefAtts, value, -1);
                Tcl_DStringAppend(&dsHrefAtts, "\" ", 2);
            }
        }

	if (link == NULL) {
	    link = deflink;
	    if (page == NULL) {
	    	page = defpage;
	    }
	} else if (page == NULL) {
	    page = "";
	}
	index = FindLink(links, &nlinks, link, page, dsHrefAtts.string);
	if (index < 0) {
	    AppendText(&ds, t);
	} else {
	    c = (char) index;
	    Ns_DStringNAppend(&ds, "l", 1);
	    Ns_DStringNAppend(&ds, &c, 1);
	    Ns_DStringAppendArg(&ds, t);
	}
    	p = e;
    }
    AppendText(&ds, p);
    len = ds.length+1;

    Tcl_DStringInit(&tds);
    for (i = 0; i < nlinks; ++i) {
    	Tcl_DStringStartSublist(&tds);
	Tcl_DStringAppendElement(&tds, links[i]->link);
	Tcl_DStringAppendElement(&tds, links[i]->page);
	Tcl_DStringAppendElement(&tds, links[i]->hrefAtts);
	Tcl_DStringEndSublist(&tds);
	ns_free(links[i]);
    }
    if (Tcl_SetVar(interp, argv[3], tds.string, TCL_LEAVE_ERR_MSG) == NULL
    	|| DciWriteChan(interp, argv[1], ds.string, len) != TCL_OK) {
    	result = TCL_ERROR;
    } else {
    	sprintf(interp->result, "%d", len);
	result = TCL_OK;
    }
    Ns_SetFree(setPtr);
    Tcl_DStringFree(&tds);
    Tcl_DStringFree(&dsHrefAtts);
    Ns_DStringFree(&ds);
    ns_free(buf);
    return result;
}
