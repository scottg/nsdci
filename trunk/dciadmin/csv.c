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

static int
CsvAppend(Tcl_Interp *interp, char *field, Tcl_DString *dsPtr)
{
    Ns_DString ds;
    int len;
    char *p, *s = field;
    
    if (s == NULL) {
        return TCL_ERROR;
    }
    len = strlen(s);
    Ns_DStringInit(&ds);
    if (*s == '"' && *(s+len-1) == '"') {
        *(s+len-1) = '\0';
        s += 1;
    }
    while ((p = strstr(s, "\"\"")) != NULL) {
        *p = '\0';
        Ns_DStringVarAppend(&ds, s, "\"", NULL);
        s = p+2;
    }
    Ns_DStringAppend(&ds, s);
    Tcl_DStringAppendElement(dsPtr, ds.string);
    Ns_DStringFree(&ds);
    
    return TCL_OK;
}


static int
CsvGet(Tcl_Interp *interp, Tcl_Channel channel, Tcl_DString *dsPtr, int *nColumns)
{
    int n, len, cols, inquote;
    char *p, *s = NULL;
    Tcl_DString dsLine;
    
    cols = inquote = 0;
    *nColumns = -1;
    Tcl_DStringInit(&dsLine);
    while (1) {
        len = Tcl_DStringLength(&dsLine);
        n = Tcl_Gets(channel, &dsLine);
        if (n < 0) {
            Tcl_DStringFree(&dsLine);
            if (!Tcl_Eof(channel)) {
                Tcl_AppendResult(interp, Tcl_PosixError(interp), NULL);
                return TCL_ERROR;
            } else {
                return TCL_OK;
            }
        }
        p = dsLine.string + len;
        if (!inquote) {
            s = p;
            if (*p == '"') {
                inquote = 1;
                p++;
            }
        }
        for (; *p != '\0'; p++) {
            if (*p == '"' && *(p+1) == '"') {
                p++;
            } else if (*p == '"') {
                inquote ^= 1;
            } else {
                if (*p == ',' && !inquote) {
                    *p = '\0';
                    CsvAppend(interp, s, dsPtr);
                    *p = ',';
                    cols += 1;
                    s = p+1;
                }
            }
        }
        if (!inquote) {
            CsvAppend(interp, s, dsPtr);
            cols += 1;
            break;
        }
        Tcl_DStringAppend(&dsLine, "\n", 1);
    }
    *nColumns = cols;
    Tcl_DStringFree(&dsLine);
    
    return TCL_OK;
}

int
Dci_CsvGetCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{

    Tcl_Channel channel;
    Tcl_DString ds;
    int nCols;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " fileId varName\"", NULL);
        return TCL_ERROR;
    }
    
    if (Ns_TclGetOpenChannel(interp, argv[1], 0, 0, &channel) == TCL_ERROR) {
        return TCL_ERROR;
    }
    
    Tcl_DStringInit(&ds);
    CsvGet(interp, channel, &ds, &nCols);
    if (Tcl_SetVar(interp, argv[2], ds.string, TCL_LEAVE_ERR_MSG) == NULL) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    Tcl_DStringFree(&ds);
    sprintf(interp->result, "%d", nCols);
    
    return TCL_OK;
}
