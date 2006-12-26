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

static int CmpIdent(const void *p1, const void *p2);
static Tcl_CmdProc InfoCmd, IdentsCmd, EvalConfigCmd, ModInitCmd;
static Tcl_DString identsBuf;
static int nidents;
static char **idents;
char *dciDir;

void
DciLibInit(void)
{
    static int once = 0;

    if (!once) {
	once = 1;

    	Tcl_DStringInit(&identsBuf);
    	DciAddIdent(rcsid);

    	/*
     	 * Add library load time inits here.
     	 */
	
    	DciAgfLibInit();
    	DciAscii85LibInit();
    	DciAvLibInit();
    	DciBase64LibInit();
    	DciBrangeLibInit();
    	DciCacheLibInit();
    	DciCbApiLibInit();
    	DciCmdInfoLibInit();
    	DciCompat30LibInit();
    	DciDatenumLibInit();
    	DciEvalLibInit();
    	DciFileLibInit();
    	DciGeoLibInit();
    	DciHtmlLibInit();
    	DciHttpLibInit();
    	DciImgLibInit();
    	DciListLibInit();
    	DciMiscLibInit();
    	DciNvLibInit();
    	DciPageLibInit();
    	DciParseLibInit();
    	DciProxyLibInit();
    	DciSlLibInit();
    	DciSockLibInit();
    	DciStrftimeLibInit();
    	DciTclExportLibInit();
    	DciTimeLibInit();
    	DciUnixLibInit();
    	DciUuidLibInit();
    }
}


int
Dci_Init(Tcl_Interp *interp)
{
    /*
     * Add Tcl interp init calls here.
     */

    if (DciAgfTclInit(interp) != TCL_OK ||
	DciAvTclInit(interp) != TCL_OK ||
	DciAscii85TclInit(interp) != TCL_OK ||
	DciBase64TclInit(interp) != TCL_OK ||
	DciBrangeTclInit(interp) != TCL_OK ||
	DciCacheTclInit(interp) != TCL_OK ||
	DciDatenumTclInit(interp) != TCL_OK ||
	DciFileTclInit(interp) != TCL_OK ||
	DciGeoTclInit(interp) != TCL_OK ||
	DciHtmlTclInit(interp) != TCL_OK ||
	DciHttpTclInit(interp) != TCL_OK ||
	DciImgTclInit(interp) != TCL_OK ||
	DciMiscTclInit(interp) != TCL_OK ||
	DciNvTclInit(interp) != TCL_OK ||
	DciParseTclInit(interp) != TCL_OK ||
	DciPageTclInit(interp) != TCL_OK ||
	DciProxyTclInit(interp) != TCL_OK ||
	DciSlTclInit(interp) != TCL_OK ||
	DciTimeTclInit(interp) != TCL_OK ||
	DciUnixTclInit(interp) != TCL_OK ||
	DciUuidTclInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_CreateCommand(interp, "dci.info", InfoCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.idents", IdentsCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.evalconfig", EvalConfigCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.modinit", ModInitCmd, NULL, NULL);
    Tcl_PkgProvide(interp, "Dci", "5.0");
    return TCL_OK;
}


int
DciModInit(char *server, char *module)
{
    int i;

    dciDir = Ns_ConfigGetValue("dci", "root");
    if (dciDir == NULL) {
	Ns_DString ds;

	Ns_DStringInit(&ds);
	Ns_ModulePath(&ds, server, module, NULL);
	dciDir = Ns_DStringExport(&ds);
    }
    if (mkdir(dciDir, 0755) != 0 && errno != EEXIST) {
	Ns_Log(Warning, "mkdir(%s) failed: %s", dciDir, strerror(errno));
    }

    /*
     * The following inits can execute in any order.
     */

    if (DciAgfModInit(server, module) != NS_OK ||
    	DciArtInit(server, module) != NS_OK ||
	DciAvModInit(server, module) != NS_OK ||
	DciCfgInit(server, module) != NS_OK ||
	DciCbInit(server, module) != NS_OK ||
	DciGdbmInit(server, module) != NS_OK ||
	DciImgModInit(server, module) != NS_OK ||
	DciLogInit(server, module) != NS_OK ||
	DciNetEvalInit(server, module) != NS_OK ||
	DciProxyModInit(server, module) != NS_OK ||
	DciRpcInit(server, module) != NS_OK ||
	DciServerInit(server, module) != NS_OK ||
	DciReceiverInit(server, module) != NS_OK) {
	return NS_ERROR;
    }

    /*
     * The following inits depend on the inits above.
     */

    if (DciNtInit(server, module) != NS_OK ||
	DciNetdbInit(server, module) != NS_OK ||
	DciBroadcastInit(server, module) != NS_OK ||
	DciNfsInit(server, module) != NS_OK ||
	DciNcfInit(server, module) != NS_OK ||
	DciNetProxyInit(server, module) != NS_OK ||
	DciNrateInit(server, module) != NS_OK ||
	DciNvModInit(server, module) != NS_OK ||
	DciNpInit(server, module) != NS_OK ||
	DciSobInit(server, module) != NS_OK) {
	return NS_ERROR;
    }

    return NS_OK;
}


void
DciAddIdent(char *ident)
{
   Tcl_DStringAppend(&identsBuf, (char *) &ident, sizeof(char *));
   idents = (char **) identsBuf.string;
   ++nidents;
    qsort(idents, nidents, sizeof(char *), CmpIdent);
}


void
Dci_LogIdent(char *module, char *ident)
{
    DciAddIdent(ident);
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


static int
IdentsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int i;

    for (i = 0; i < nidents; ++i) {
	Tcl_AppendElement(interp, idents[i]);
    }
    return TCL_OK;
}


static int
InfoCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
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


static int
EvalConfigCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    extern void NsConfigEval(char *cfg, int argc, char **argv, int optind);

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
		argv[0], " script server ?module? ?args?\"", NULL);
	return TCL_ERROR;
    }
    NsConfigEval(argv[1], argc-2, argv+2, 0);
    return TCL_OK;
}


static int
ModInitCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " server module\"", NULL);
	return TCL_ERROR;
    }
    DciModInit(argv[1], argv[2]);
    return TCL_OK;
}


static int
CmpIdent(const void *p1, const void *p2)
{
    char *s1 = *(char **) p1;
    char *s2 = *(char **) p2;
    return strcmp(s1, s2);
}

