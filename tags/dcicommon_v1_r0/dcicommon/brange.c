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
#include <sys/stat.h>

#define BUFSIZE 255

static char rcsid[] = "$Id$";
static Tcl_CmdProc brangeCmd;
static void seekAndScan(Tcl_Channel fp, int recNum, int recSize, char* key, int *val);


void
DciBrangeLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciBrangeTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.brange", brangeCmd, NULL, NULL);
    return TCL_OK;
}


static int
brangeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Channel fp;
    char buf[BUFSIZE], key[BUFSIZE];
    Tcl_DString dynLine;
    int recSize, fileSize, numRecs, curRec, minRec, maxRec, foundRec, comp, val, numComp , found;
    struct stat stbuf;

    recSize = fileSize = numRecs = curRec = minRec = maxRec = foundRec = comp = val = numComp = found = 0;
    Tcl_DStringInit(&dynLine);

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " file string\"", NULL);
        return TCL_ERROR;
    }

    fp = Tcl_OpenFileChannel(interp, argv[1], "r", 777);
    if (fp == NULL)  {
        Tcl_AppendResult(interp, "unable to read file \"", argv[0], "\"", NULL);
        return TCL_ERROR;
    }

    stat(argv[1],&stbuf);
    fileSize = stbuf.st_size;

    recSize = Tcl_Gets(fp, &dynLine);
    recSize++;
    Tcl_DStringSetLength(&dynLine, 0);
    Tcl_DStringFree(&dynLine);

    numRecs = fileSize / recSize;

    minRec = 1;
    maxRec = numRecs;
    while (1) {
        curRec = (maxRec + minRec) / 2;
        seekAndScan(fp, curRec, recSize, key, &val);

        if (Tcl_StringMatch(key, argv[2])) {
            found++;
            break;
        } else if (maxRec == minRec) {
            break;
        }

        comp = strcmp(key,argv[2]);
        if (comp > 0) {
            maxRec = curRec - 1;
        } else if (comp < 0) {
            minRec = curRec + 1;
        } else {
            break;
        }
        if (++numComp > 64) {
            break;
        }
    }

    if (found) {
        foundRec = curRec;

        sprintf(buf, "%s %d", key, val);
        
        Tcl_AppendElement(interp, buf);
        curRec++;
        while ((found) && (curRec <= numRecs)) {
            seekAndScan(fp, curRec, recSize, key, &val);
            if (Tcl_StringMatch(key, argv[2])) {
                sprintf(buf, "%s %d", key, val);
                Tcl_AppendElement(interp, buf);
                curRec++;
            } else {
                found--;
            }
        }

        found++;
        curRec = foundRec - 1;
        while ((found) && (curRec > 0)) {
            seekAndScan(fp, curRec, recSize, key, &val);
            if (Tcl_StringMatch(key, argv[2])) {
                sprintf(buf, "%s %d", key, val);
                Tcl_AppendElement(interp, buf);
                curRec--;
            } else {
                found--;
            }
        }

    }
    Tcl_Close(interp, fp);
    return TCL_OK;
}

static void 
seekAndScan(Tcl_Channel fp, int recNum, int recSize, char* key, int *val)
{
    char scanbuf[BUFSIZE];

    Tcl_Seek(fp,(Tcl_WideInt)(((recNum - 1) * recSize)), SEEK_SET);
    Tcl_Read(fp, scanbuf, recSize);
    sscanf(scanbuf, "%s %d", key, val);
}
