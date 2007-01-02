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
#include <locale.h>

static char rcsid[] = "$Id$";

/*
 * Non-exported symbols.
 */
extern void DciGeoLibInit(void);


char *dciDir;
static Tcl_DString idents;

void
DciInit(void)
{
    Tcl_DStringInit(&idents);
    DciGeoLibInit();
}


void
Dci_LogIdent(char *module, char *ident)
{
    Tcl_DStringAppendElement(&idents, ident);
    Ns_Log(Notice, "%s: ident: %s", module, ident);
}


static int
DciIdentsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_SetResult(interp, idents.string, TCL_VOLATILE);
    return TCL_OK;
}


static int
DciInfoCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[100];

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " command\"", NULL);
	return TCL_ERROR;
    }
    if (STREQ(argv[1], "dir")) {
    	Tcl_SetResult(interp, dciDir, TCL_STATIC);
    } else if (STREQ(argv[1], "minor")) {
    	Dci_SetIntResult(interp, DCI_MINOR);
    } else if (STREQ(argv[1], "major")) {
    	Dci_SetIntResult(interp, DCI_MAJOR);
    } else if (STREQ(argv[1], "version")) {
    	sprintf(buf, "%d.%d", DCI_MAJOR, DCI_MINOR);
    	Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else if (STREQ(argv[1], "id")) {
    	Tcl_SetResult(interp, rcsid, TCL_STATIC);
    } else if (STREQ(argv[1], "build")) {
    	Tcl_SetResult(interp, __DATE__ " at " __TIME__, TCL_STATIC);
    } else {
	Tcl_AppendResult(interp, "unknown command \"", 
	    argv[1], "\": should be dir, major, minor, id, or build", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


int
DciAddCmds(Tcl_Interp *interp)
{
    extern Tcl_CmdProc Dci_ShutdownPendingCmd, Dci_SetThreadServerCmd,
	Dci_IsCmd, Dci_StripCmd, Dci_HashCmd, Dci_RandomCmd,
	Dci_DeferredCmd, Dci_WordTruncCmd, Dci_StripCharCmd,
	Dci_CleanStringCmd, Dci_ReplaceStringCmd, Dci_KeylgetCmd,
	Dci_UserHomeCmd, Dci_CleanSNCmd, Dci_GetUuidCmd,
	Dci_StrptimeCmd, Dci_StrftimeCmd, Dci_AmsGetCmd,
	Dci_NextAdIdCmd, Dci_Base64Cmd, Dci_JpegSizeCmd, Dci_ImgCmd,
	Dci_GifSizeCmd, Dci_BmpSizeCmd, Dci_ImgTypeCmd, Dci_ImgSizeCmd,
    Dci_MkdirCmd, Dci_ChmodCmd, Dci_UnlinkCmd, Dci_RmCmd, Dci_PidCmd,
	Dci_RenameCmd, Dci_LinkCmd, Dci_SymlinkCmd, Dci_TruncateCmd,
	Dci_FtruncateCmd, Dci_RmdirCmd, Dci_SocketPairCmd,
	Dci_LockFpCmd, Dci_FsyncCmd, Dci_ReadFileCmd, Dci_WriteFileCmd,
	Dci_ReadFile2Cmd, Dci_WriteFile2Cmd, Dci_RealpathCmd,
	Dci_WriteNullCmd, Dci_PinfoCmd, Dci_MondirCmd, Dci_GroupIntCmd,
	Dci_TagCheckCmd, Dci_Dn2DateCmd, Dci_Time2DnCmd,
	Dci_Date2DnCmd, Dci_Dn2DowCmd, Dci_UtimeCmd,
	Dci_GetWeekCmd, Dci_Week2GregorianCmd, DciGeoBoxCmd, DciGeoDistanceCmd,
        Dci_Ascii85Cmd;


    Tcl_CreateCommand(interp, "dci.idents", DciIdentsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.info", DciInfoCmd, NULL, NULL);

    /*
     * misc.c
     */

    Tcl_CreateCommand(interp, "dci.shutdownPending", Dci_ShutdownPendingCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.setthreadserver", Dci_SetThreadServerCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.pid", Dci_PidCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.isint", Dci_IsCmd, (ClientData) 'i', NULL);
    Tcl_CreateCommand(interp, "dci.isbool", Dci_IsCmd, (ClientData) 'b', NULL);
    Tcl_CreateCommand(interp, "dci.isdouble", Dci_IsCmd, (ClientData) 'd', NULL);
    Tcl_CreateCommand(interp, "dci.strip", Dci_StripCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.random", Dci_RandomCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.hash", Dci_HashCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.deferred", Dci_DeferredCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.wordtrunc", Dci_WordTruncCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.stripCRs", Dci_StripCharCmd, (ClientData) '\r', NULL);
    Tcl_CreateCommand(interp, "dci.cleanString", Dci_CleanStringCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.subString", Dci_ReplaceStringCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.keylget", Dci_KeylgetCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.writenull", Dci_WriteNullCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.groupint", Dci_GroupIntCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.checktags", Dci_TagCheckCmd, NULL, NULL);

    /*
     * uuid.c
     */

    Tcl_CreateCommand(interp, "dci.uuid_create", Dci_GetUuidCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.getuuid", Dci_GetUuidCmd, NULL, NULL);

    /*
     * time.c
     */

    Tcl_CreateCommand(interp, "dci.strptime", Dci_StrptimeCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.strftime", Dci_StrftimeCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.strftimef", Dci_StrftimeCmd, (ClientData) 'f', NULL);
    Tcl_CreateCommand(interp, "dci.strftime2", Dci_StrftimeCmd, (ClientData) 'g', NULL);
    Tcl_CreateCommand(interp, "log.getWeek", Dci_GetWeekCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.week2date", Dci_Week2GregorianCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.getWeek", Dci_GetWeekCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.week2date", Dci_Week2GregorianCmd, NULL, NULL);

    /*
     * base64.c
     */

    Tcl_CreateCommand(interp, "base64.encode", Dci_Base64Cmd, (ClientData) 'e', NULL);
    Tcl_CreateCommand(interp, "base64.decode", Dci_Base64Cmd, (ClientData) 'd', NULL);

    /*
     * ascii85.c
     */
    Tcl_CreateCommand(interp, "ascii85.encode", Dci_Ascii85Cmd, (ClientData) 'e', NULL);
    Tcl_CreateCommand(interp, "ascii85.decode", Dci_Ascii85Cmd, (ClientData) 'd', NULL);

    /*
     * img.c
     */
    Tcl_CreateCommand(interp, "dci.jpegsize", Dci_ImgSizeCmd, JpegSize, NULL);
    Tcl_CreateCommand(interp, "dci.gifsize",  Dci_ImgSizeCmd, GifSize, NULL);
    Tcl_CreateCommand(interp, "dci.bmpsize",  Dci_ImgSizeCmd, BmpSize, NULL);
    Tcl_CreateCommand(interp, "dci.imgtype",  Dci_ImgCmd, (ClientData) 't', NULL);
    Tcl_CreateCommand(interp, "dci.imgsize",  Dci_ImgCmd, (ClientData) 's', NULL);

    /*
     * unix.c
     */

    Tcl_CreateCommand(interp, "dci.chmod", Dci_ChmodCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.mkdir", Dci_MkdirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.unlink", Dci_UnlinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rm", Dci_RmCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rename", Dci_RenameCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.truncate", Dci_TruncateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.ftruncate", Dci_FtruncateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rmdir", Dci_RmdirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.fsync", Dci_FsyncCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.realpath", Dci_RealpathCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.writefile", Dci_WriteFileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.appendfile", Dci_WriteFileCmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.readfile", Dci_ReadFileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.readFile", Dci_ReadFileCmd, (ClientData) 1, NULL);
    Tcl_CreateCommand(interp, "dci.writeFile", Dci_WriteFile2Cmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.appendFile", Dci_WriteFile2Cmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.atime", Dci_UtimeCmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.mtime", Dci_UtimeCmd, (ClientData) 'm', NULL);
    Tcl_CreateCommand(interp, "dci.link", Dci_LinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.symlink", Dci_SymlinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.lockfp", Dci_LockFpCmd, NULL, NULL);

    /*
     * datenum.c
     */

    Tcl_CreateCommand(interp, "dci.time2dn", Dci_Time2DnCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.date2dn", Dci_Date2DnCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.dn2date", Dci_Dn2DateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.dn2plsdate", Dci_Dn2DateCmd, (ClientData) 'p', NULL);
    Tcl_CreateCommand(interp, "dci.dn2dow", Dci_Dn2DowCmd, NULL, NULL);

    /*
     * geo.c
     */

    Tcl_CreateCommand(interp, "geo.box", DciGeoBoxCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "geo.distance", DciGeoDistanceCmd, NULL, NULL);

    return TCL_OK;
}


int
DciSetDebugCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int *debugPtr;
    int new, old;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?reset?\"", NULL);
	return TCL_ERROR;
    }
    debugPtr = arg;
    old = *debugPtr;
    if (argc == 2) {
	if (Tcl_GetBoolean(interp, argv[1], &new) != TCL_OK) {
	   return TCL_ERROR;
	}
	*debugPtr = new;
    }
    Tcl_SetResult(interp, old ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}
