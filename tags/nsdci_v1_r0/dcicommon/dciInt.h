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

#ifndef DCIINT_H
#define DCIINT_H

#include "dci.h"
#include "gdbm.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <math.h>
#include <utime.h>
#include <sys/param.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <netinet/tcp.h>
#include <limits.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <poll.h>

extern char *dciDir;

#ifdef UCHAR
#undef UCHAR
#endif
#define UCHAR(c) ((unsigned char) (c))
#ifdef STREQ
#undef STREQ
#endif
#define STREQ(a,b) (((*a) == (*b)) && (strcmp((a),(b)) == 0))

/*
 *  DciTclExport message formats.
 *   These represent variable ways in which the
 *   Result (R), errorcode (C), and errorinformation (I) fields
 *   can be ordered in the response message.
 */
#define DCI_EXPORTFMT_CIR   "CIR"
#define DCI_EXPORTFMT_RCI   "RCI"
#define DCI_EXPORTFMT_NPROXY DCI_EXPORTFMT_CIR
#define DCI_EXPORTFMT_NEVAL  DCI_EXPORTFMT_RCI

typedef void (DciLibInitProc)(void);
extern DciLibInitProc 
	DciAgfLibInit,
	DciAscii85LibInit,
	DciAvLibInit,
	DciBase64LibInit,
	DciBrangeLibInit,
	DciCacheLibInit,
	DciCbApiLibInit,
	DciCmdInfoLibInit,
	DciCompat30LibInit,
	DciDatenumLibInit,
	DciEvalLibInit,
	DciFileLibInit,
	DciGeoLibInit,
	DciHtmlLibInit,
	DciHttpLibInit,
	DciImgLibInit,
	DciListLibInit,
	DciMiscLibInit,
	DciNvLibInit,
	DciPageLibInit,
	DciParseLibInit,
	DciProxyLibInit,
	DciSlLibInit,
	DciSockLibInit,
	DciStrftimeLibInit,
	DciTclExportLibInit,
	DciTimeLibInit,
	DciUnixLibInit,
	DciUuidLibInit;

extern Tcl_AppInitProc
	DciAgfTclInit,
	DciAscii85TclInit,
	DciAvTclInit,
	DciBase64TclInit,
	DciBrangeTclInit,
	DciCacheTclInit,
	DciDatenumTclInit,
	DciFileTclInit,
	DciGeoTclInit,
	DciHtmlTclInit,
	DciHttpTclInit,
	DciImgTclInit,
	DciListTclInit,
	DciMiscTclInit,
	DciNvTclInit,
	DciParseTclInit,
	DciPageTclInit,
	DciProxyTclInit,
	DciSlTclInit,
	DciTimeTclInit,
	DciUnixTclInit,
	DciUuidTclInit;

extern Ns_ModuleInitProc
	DciModInit,
	DciAgfModInit,
	DciArtInit,
	DciAvModInit,
	DciBroadcastInit,
	DciCfgInit,
	DciCbInit,
	DciGdbmInit,
	DciImgModInit,
	DciLogInit,
	DciNetdbInit,
	DciNetEvalInit,
	DciNcfInit,
	DciNetProxyInit,
	DciNfsInit,
	DciNrateInit,
	DciNpInit,
	DciNtInit,
	DciNvModInit,
	DciProxyModInit,
	DciReceiverInit,
	DciRpcInit,
	DciServerInit,
	DciSobInit;

/*
 * tclexport.c
 */

extern void DciTclExport(Tcl_Interp *interp, int code, Ns_DString *dsPtr,
			 char *x_fmt);
extern int DciTclImport(Tcl_Interp *interp, Ns_DString *dsPtr, char *x_fmt);
extern int DciTclRead(int fd, Ns_DString *dsPtr, int net);
extern int DciTclRecv(int sock, Ns_DString *dsPtr, int timeout);

/*
 * init.c
 */

extern void DciProxyMain(int argc, char **argv);
extern Tcl_AppInitProc DciAppInit;
extern void DciAddIdent(char *ident);
extern Tcl_CmdProc DciSetDebugCmd;

/*
 * gdbm.c
 */

extern GDBM_FILE Dci_GdbmOpen(char *file);

/*
 * http.c
 */

extern int DciHttpGet(Tcl_Interp *interp, char *method, char *url,
		      Ns_Set *hdrs, int ms);

/*
 * sock.c
 */

extern char *DciGetPeer(int sock);
extern void DciLogPeer(int sock, char *action);
extern void DciLogPeer2(int sock, char *ident, char *action);
extern void Dci_UpdateIov(struct iovec *iov, int iovcnt, int nsend);
extern void Dci_SockOpts(int sock, int type);

/*
 * receiver.c
 */

extern int DciAppendLog(char *log, char *string);
extern int DciGetCommandInfo(Tcl_Interp *, char *cmd,
		Tcl_CmdProc **procPtrPtr, ClientData *dataPtr);

/*
 * compat30.c
 */

extern int DciGetOpenFd(Tcl_Interp *, char *fid, int write, int *fdPtr);
extern int DciFlush(Tcl_Interp *, char *fid);

#endif
