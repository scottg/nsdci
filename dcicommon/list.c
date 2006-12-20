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

static int CmpElem(const void *e1, const void *e2);

void
DciListLibInit(void)
{
    DciAddIdent(rcsid);
}

/*
 * Initialize a new list, splitting and sorting the given list string.
 */

int
Dci_ListInit(Dci_List *listPtr, char *list)
{
    int listc;
    char **listv;

    if (Tcl_SplitList(NULL, list, &listc, &listv) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (listc & 1) {
	ckfree((char *) listv);
	return TCL_ERROR;
    }
    listPtr->nelem = listc / 2;
    listPtr->elems = (Dci_Elem *) listv;
    qsort(listPtr->elems, listPtr->nelem, sizeof(Dci_Elem), CmpElem);
    return TCL_OK;
}

/*
 * Search a sorted list.
 */

Dci_Elem *
Dci_ListSearch(Dci_List *listPtr, char *key)
{
    Dci_Elem elem, *elemPtr;

    elem.key = key;
    elem.value = NULL;
    elemPtr = bsearch(&elem, listPtr->elems, (size_t) listPtr->nelem,
		      sizeof(Dci_Elem), CmpElem);
    return elemPtr;
}

/*
 * Free sorted list.
 */

void
Dci_ListFree(Dci_List *listPtr)
{
    ckfree((char *) listPtr->elems);
}


void
Dci_ListDump(Tcl_Interp *interp, Dci_List *listPtr, char *pattern, int values)
{
    char *key;
    int i;

    for (i = 0; i < listPtr->nelem; ++i) {
	key = Dci_ListKey(listPtr, i);
	if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
	    Tcl_AppendElement(interp, key);
	    if (values) {
	    	Tcl_AppendElement(interp, Dci_ListValue(listPtr, i));
	    }
	}
    }
}


static int
CmpElem(const void *e1, const void *e2)
{
    Dci_Elem *s1, *s2;

    s1 = (Dci_Elem *) e1;
    s2 = (Dci_Elem *) e2;
    return strcmp(s1->key, s2->key);
}

