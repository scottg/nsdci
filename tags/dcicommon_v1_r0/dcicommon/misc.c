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
#include <locale.h>
        
static Tcl_CmdProc SetThreadServerCmd, ShutdownPendingCmd, PidCmd,
	IsCmd, RandomCmd, HashCmd, StripCmd, TagCheckCmd, DeferredCmd,
	WordTruncCmd, ReplaceStringCmd, StripCharCmd, CleanStringCmd,
	KeylgetCmd, WriteNullCmd, GroupIntCmd;


void
DciMiscLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciMiscTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.shutdownPending", ShutdownPendingCmd,
		      NULL, NULL);
    Tcl_CreateCommand(interp, "dci.setthreadserver", SetThreadServerCmd,
		      NULL, NULL);
    Tcl_CreateCommand(interp, "dci.pid", PidCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.isint", IsCmd, (ClientData) 'i', NULL);
    Tcl_CreateCommand(interp, "dci.isbool", IsCmd, (ClientData) 'b', NULL);
    Tcl_CreateCommand(interp, "dci.isdouble", IsCmd, (ClientData) 'd', NULL);
    Tcl_CreateCommand(interp, "dci.strip", StripCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.random", RandomCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.hash", HashCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.deferred", DeferredCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.wordtrunc", WordTruncCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.stripCRs", StripCharCmd, (ClientData) '\r',
		      NULL);
    Tcl_CreateCommand(interp, "dci.cleanString", CleanStringCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.subString", ReplaceStringCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.keylget", KeylgetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.writenull", WriteNullCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.groupint", GroupIntCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.checktags", TagCheckCmd, NULL, NULL);
    return TCL_OK;
}


void
Dci_SetIntResult(Tcl_Interp *interp, int i)
{
    char buf[100];

    sprintf(buf, "%d", i);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
}


static int
SetThreadServerCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " name\"", NULL);
	return TCL_ERROR;
    }
    Ns_ThreadSetName(argv[1]);
    return TCL_OK;
}


static int
ShutdownPendingCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_SetIntResult(interp, Ns_InfoShutdownPending());
    return TCL_OK;
}


static int
PidCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Dci_SetIntResult(interp, Ns_InfoPid());
    return TCL_OK;
}


static int
IsCmd(ClientData type,Tcl_Interp *interp, int argc, char **argv)
{
    int i;
    int code = TCL_ERROR;
    double d;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " var\"", NULL);
	return TCL_ERROR;
    }
    switch ((int) type) {
	case 'i':
            code = Tcl_GetInt(interp, argv[1], &i);
	break;
	case 'b':
            code = Tcl_GetBoolean(interp, argv[1], &i);
	break;
	case 'd':
            code = Tcl_GetDouble(interp, argv[1], &d);
	break;
    }
    if (code == TCL_OK) {
        Tcl_SetResult(interp, "1", TCL_STATIC);
    } else {
    	Tcl_SetResult(interp, "0", TCL_STATIC);
    }
    return TCL_OK;
}


static int
RandomCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    char buf[100];
    
    if (argc > 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?ignored?\"", NULL);
        return TCL_ERROR;
    }
    Tcl_PrintDouble(interp, Ns_DRand(), buf);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}

static int
HashCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    register unsigned int result;
    register int c;
    register char *string;
    int buckets;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " string buckets\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &buckets) != TCL_OK) {
	return TCL_ERROR;
    }
    string = argv[1];
    result = 0; 
    while ((c = *string) != 0) {
	++string;
	result += (result<<3) + c;
    }
    Dci_SetIntResult(interp, (int)(result % buckets));
    return TCL_OK;
}
    
void
Dci_FindTagAtts(Ns_Set *setPtr, char *start, size_t len)
{
    char *strPtr, *e, *s, *q, *k, *v, save;
    int isKey = 1;
    int inQuote = 0;
    
    if (!len) return;
    
    strPtr = ns_malloc(len+1);
    memset(strPtr, 0, len+1);
    strncat(strPtr, start, len);
    k = q = strPtr;
    v = NULL;
    
    while (*q) {
        if (*q == '"') {
            inQuote ^= 1;
        } else if (*q == '=' && !inQuote) {
            e = q;
            while (isspace(UCHAR(*e)) || *e == '"' || *e == '=') --e;
            *(e + 1) = '\0';
            v = q + 1;
            isKey = 0;
        } else if (*q == ' ' && !inQuote && !isKey) {
            e = q - 1;
            while (isspace(UCHAR(*k)) || *k == '"') ++k;
            while (isspace(UCHAR(*v)) || *v == '"') ++v;
            while (isspace(UCHAR(*e)) || *e == '"') --e;
            ++e;
            save = *e;
            *e = '\0';
            Ns_SetPut(setPtr, k, v);
            *e = save;
            k = s = q;
            v = NULL;
            isKey = 1;
        }
        ++q;
    }
    if (v != NULL && !inQuote && !isKey) {
        e = q - 1;
        while (isspace(UCHAR(*k)) || *k == '"') ++k;
        while (isspace(UCHAR(*v)) || *v == '"') ++v;
        while (isspace(UCHAR(*e)) || *e == '"') --e;
        *(e + 1) = '\0';
        Ns_SetPut(setPtr, k, v);
    }
    ns_free(strPtr);
}
    
static char *
FindTag(char *s, char *tag, int isend, char **pend, Ns_Set *setPtr)
{
    size_t len;
    register char *p, *q, *a, *c;

    len = strlen(tag);    
    do {
        p = strchr(s++, '<');
	if (p != NULL) {
	    q = p;
	    while (isspace(UCHAR(*++q)));
	    if (isend) {
    		if (*q != '/') {
		    continue;
    		}
		while (isspace(UCHAR(*++q)));
	    }
	    if (strncasecmp(q, tag, len) == 0) {
		q += len;
		if (!isspace(UCHAR(*q)) && *q != '>') {
		    continue;
		}
                if (!isend) {
                    a = c = q;
                    while (*c && *c != '>') {
                        ++c;
                    }
                    Dci_FindTagAtts(setPtr, a, (size_t)(c - a));
                }
                while (*q && *q != '>') ++q;
		if (*q == '>') {
    		    *pend = ++q;
		    return p;
		}
	    }
	}
    } while (p != NULL);
    return p;
}


static int
StripCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    char *command = NULL; /* to quiet compiler */
    char *string, *tag, *ps, *pe, *qs, *qe;
    int code;
    Ns_Set *setPtr;
    enum {INCLUDE, EXCLUDE, COMMAND} stripMode = INCLUDE; /* to quiet compiler */
    Tcl_DString result, script;
    
    if ((argc < 3) || (argc > 5)) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " ?-include|-exclude|-command script? tag string\"",
                (char *) NULL);
        return TCL_ERROR;
    }
    if (argc == 3) {
	stripMode = INCLUDE;
    } else if (argc == 4) {
    	if (STREQ(argv[1], "-include")) {
    	    stripMode = INCLUDE;
    	} else if (STREQ(argv[1], "-exclude")) {
    	    stripMode = EXCLUDE;
    	} else {    
badswitch:
            Tcl_AppendResult(interp, "bad switch \"", argv[0],
                            "\": must be -include, -exclude, or -command", (char *) NULL);
            return TCL_ERROR;
        }
    } else if (argc == 5) {
    	if (!STREQ(argv[1], "-command")) {
    	    goto badswitch;
    	}
        stripMode = COMMAND;
        command = argv[2];
    }

    tag = argv[argc-2];
    string = argv[argc-1];
 
    Tcl_DStringInit(&result);
    Tcl_DStringInit(&script);
    code = TCL_OK;
    do {
        setPtr = Ns_SetCreate("tagAttributes");
    	ps = FindTag(string, tag, 0, &pe, setPtr);
        qs = NULL; /* to quiet compiler */
    	if (ps != NULL) {
	    qs = FindTag(pe, tag, 1, &qe, setPtr);
	    if (qs == NULL) {
		ps = NULL;
	    } else {
    	    	*ps = *qs = '\0';
	    }
    	}
    	Tcl_DStringAppend(&result, string, -1);
    	if (ps != NULL) {
    	    if (stripMode == INCLUDE) {
    	    	Tcl_DStringAppend(&result, pe, -1);
    	    } else if (stripMode == COMMAND) {
                Tcl_ResetResult(interp);
                Ns_TclEnterSet(interp, setPtr, NS_TCL_SET_TEMPORARY);
    	    	Tcl_DStringAppendElement(&script, command);
    	    	Tcl_DStringAppendElement(&script, pe);
                Tcl_DStringAppendElement(&script, Tcl_GetStringResult(interp));
		code = Tcl_Eval(interp, script.string);
		if (code == TCL_OK) {
    	    	    Tcl_DStringAppend(&result, Tcl_GetStringResult(interp), -1);
    	    	    Tcl_DStringTrunc(&script, 0);
		}
    	    }
    	    *ps = *qs = '<';
    	    string = qe;
    	}
        Ns_SetFree(setPtr);
    } while (code == TCL_OK && ps != NULL);
    if (code == TCL_OK) {
    	Tcl_DStringResult(interp, &result);
    }
    Tcl_DStringFree(&result);
    Tcl_DStringFree(&script);
    return code;
}


static int
TagCheckCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    char **tagv, *p, *start, *end, *name, *c;
    int tagc, i, code;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " html goodTagList\"",
                (char *) NULL);
        return TCL_ERROR;
    }

    if (Tcl_SplitList(interp, argv[2], &tagc, &tagv) != TCL_OK) {
        Tcl_SetResult(interp, "could not split goodTagList", TCL_STATIC);
        return TCL_ERROR;
    }

    for (i=0; i < tagc; i++) {
        for (c = tagv[i]; *c != 0; c++) {
            if (isupper(UCHAR(*c))) {
                *c = tolower(UCHAR(*c));
            }
        }
    }

    p = argv[1];
    code = TCL_OK;
    while ((code == TCL_OK) && ((start = strchr(p, '<')) != NULL) && ((end = strchr(start, '>')) != NULL)) {
        *start++ = '\0';
        *end++ = '\0';

        while (isspace(UCHAR(*start))) ++start;
        if (*start != '\0') {
            if (*start == '/') {
                ++start;
            }
            name = start;
            while (*start != '\0' && !isspace(UCHAR(*start))) {
                if (isupper(UCHAR(*start))) {
                    *start = tolower(UCHAR(*start));
                }
                ++start;
            }
			*start = '\0';

            code = TCL_ERROR;
            for (i=0; code == TCL_ERROR && i < tagc; i++) {
                if (strcmp(name, tagv[i]) == 0) {
                    code = TCL_OK;
                }
            }
        }
        p = end;
    }

    ckfree((char *) tagv);

    Tcl_SetResult(interp, (code == TCL_OK ? "1" : "0"), TCL_STATIC);

    return TCL_OK;
}


static void
RunDeferred(Tcl_Interp *interp, void *context)
{
    char *script = (char *) context;

    if (Tcl_GlobalEval(interp, script) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    ns_free(script);
}

    
static int
DeferredCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " script\"", NULL);
	return TCL_ERROR;
    }
    Ns_TclRegisterDeferred(interp, RunDeferred, ns_strdup(argv[1]));
    return TCL_OK;
}



static int
WordTruncCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int max;
    register int ctrCnt;
    Tcl_DString ds;
    register char *start, *end;
    Tcl_UniChar uch;
    register Tcl_UniChar *uchPtr = &uch;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " string max\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &max) != TCL_OK) {
	return TCL_ERROR;
    }
    if (max <= 0) {
	Tcl_AppendResult(interp, "invalid max length: ",
	    argv[2], NULL);
	return TCL_ERROR;
    }

    end = argv[1];
    start = end;
    Tcl_DStringInit(&ds);

    do {
	while (*end && isspace(UCHAR(*end))) {
            ++end;
	}

        ctrCnt = 0;
	while (*end && !isspace(UCHAR(*end))) {
            /*
             * Use the Tcl_UtfToUniChar func simply to do a utf8-safe
             * traversal of the string; e.g., don't want 'end' to ever
             * be pointing into the middle of a multi-byte utf8
             * byte sequence.  In addition, this way, each iteration
             * through this loop represents a single char, so we can
             * count characters seen this way.
             */
            end += ((UCHAR(*end) < 0x80) ? 1 : Tcl_UtfToUniChar(end, uchPtr));
            if (++ctrCnt >= max && *end && !isspace(UCHAR(*end))) {
                Tcl_DStringAppend(&ds, start, end - start);
                Tcl_DStringAppend(&ds, " ", 1);
                start = end;
                ctrCnt = 0;
            }
	}

    } while (*end);

    if (start != end) {
        Tcl_DStringAppend(&ds, start, end - start);
    }

    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}


static int
ReplaceStringCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char save, *search, *orig, *replace, *start;
    int len;
    
    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " string searchString replaceString\"", NULL);
	return TCL_ERROR;
    }
    orig = argv[1];
    search = argv[2];
    replace = argv[3];
    
    if (*orig != '\0') {
    	len = strlen(search);
	if (len == 0) {
	    Tcl_SetResult(interp, orig, TCL_VOLATILE);
    	} else {
	    while ((start = strstr(orig, search)) != NULL) {
	    	save = *start;
        	*start = '\0';
        	Tcl_AppendResult(interp, orig, replace, NULL);
		*start = save;
        	orig = start+len;
	    }
	    Tcl_AppendResult(interp, orig, NULL);
	}
    }
    return TCL_OK;
}


static int
StripCharCmd(ClientData c, Tcl_Interp *interp, int argc, char **argv)
{
    char *p, *v;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " string\"", NULL);
	return TCL_ERROR;
    }
    
    p = argv[1];
    while (p && (v = strchr(p, (int) c))) {
        *v++ = '\0';
        Tcl_AppendResult(interp, p, NULL);
        p = v;
    }
    Tcl_AppendResult(interp, p, NULL);
    
    return TCL_OK;
}


static int
CleanStringCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *p, *v, *s;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " string\"", NULL);
	return TCL_ERROR;
    }
    s = ns_strcopy(argv[1]);
    v = p = s;
    while (*p != '\0') {
        if ((*p < ' ' && (*p != '\n' && *p != '\t')) || *p > '~') {
            *p = '\0';
            Tcl_AppendResult(interp, v, NULL);
            v = p+1;
        }
        p++;
    }
    Tcl_AppendResult(interp, v, NULL);
    ns_free(s);
    
    return TCL_OK;
}


static int
KeylgetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char           *list;
    char           *value;
    int             result;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " listvar key\"", NULL);
        return TCL_ERROR;
    }
    if (argv[2] == '\0') {
	Tcl_SetResult(interp, "null key not allowed", TCL_STATIC);
        return TCL_ERROR;
    }

    list = Tcl_GetVar(interp, argv[1], TCL_LEAVE_ERR_MSG);
    if (list == NULL) {
        return TCL_ERROR;
    }
    result = Tcl_GetKeyedListField(interp, argv[2], list, &value);
    if (result == TCL_OK) {
	Tcl_SetResult(interp, value, TCL_DYNAMIC);
    } else if (result == TCL_BREAK) {
	result = TCL_OK;
    }
    return result;
}


static int
WriteNullCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int len;
    
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " chanId string\"", NULL);
	return TCL_ERROR;
    }
    len = strlen(argv[2]) + 1;
    if (DciWriteChan(interp, argv[1], argv[2], len) != TCL_OK) {
    	return TCL_ERROR;
    }
    Dci_SetIntResult(interp, len);
    return TCL_OK;
}


static int
GroupIntCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int len, shift, interval, x, outcount, check;
    char outbuf[15];
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " int\"", NULL);
	return TCL_ERROR;
    }

    /*
     * no need to worry about buffer overrun -- Tcl_GetInt returns TCL_ERROR
     * for numbers over 4294967295, so Tcl_GetInt will fail on any string over
     * 10 characters anyways!
     */
    if (Tcl_GetInt(interp, argv[1], &check) != TCL_OK) {
        Tcl_SetResult(interp, "value is not an integer", TCL_STATIC);
        return TCL_ERROR;
    }

    len = strlen(argv[1]);
    outcount = 0;
    interval = 3;
    shift = len % interval;

    for (x = 0; x < len; x++) {
        if ((((x % interval) - shift) == 0) && (x != 0)) {
            outbuf[outcount++] = ',';
        }
        outbuf[outcount++] = argv[1][x];
    }

    outbuf[outcount] = '\0';
    Tcl_SetResult(interp, outbuf, TCL_VOLATILE);
    return TCL_OK;
}
