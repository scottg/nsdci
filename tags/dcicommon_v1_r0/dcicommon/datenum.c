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

static Tcl_CmdProc Time2DnCmd, Date2DnCmd, Dn2DateCmd, Dn2DowCmd;

/* +++Date last modified: 05-Jul-1997 */

/*
** scalar date routines    --    public domain by Ray Gardner
** Dnerically, these will work over the range 1/01/01 thru 14699/12/31.
** Practically, these only work from the beginning of the Gregorian 
** calendar thru 14699/12/31.  The Gregorian calendar took effect in
** much of Europe in about 1582, some parts of Germany in about 1700, in
** England and the colonies in about 1752ff, and in Russia in 1918.
*/

#define DNOFF 719163	/* offset from weird day to day since unix */
#define SECPERDAY 86400 /* (24 * 60 * 60) */

void
DciDatenumLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciDatenumTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.time2dn", Time2DnCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.date2dn", Date2DnCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.dn2date", Dn2DateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.dn2plsdate", Dn2DateCmd, (ClientData) 'p',
		      NULL);
    Tcl_CreateCommand(interp, "dci.dn2dow", Dn2DowCmd, NULL, NULL);
    return TCL_OK;
}

static int isleap (int yr)
{
   return yr % 400 == 0 || (yr % 4 == 0 && yr % 100 != 0);
}

static long months_to_days (int month)
{
   return (month * 3057 - 3007) / 100;
}

static long years_to_days (int yr)
{
   return yr * 365L + yr / 4 - yr / 100 + yr / 400;
}

int
Dci_Date2Dn(int yr, int mo, int day)
{
    int dn;
    
    dn = day + months_to_days(mo);
    if ( mo > 2 ) {                        /* adjust if past February */
      dn -= isleap(yr) ? 1 : 2;
    }
    yr--;
    dn += years_to_days(yr) - DNOFF;
    return dn;
}

void
Dci_Dn2Date(int dn, int *yrPtr, int *moPtr, int *dayPtr)
{
    int n;                /* compute inverse of years_to_days() */

    dn += DNOFF;
    for (n = ((dn * 400L) / 146097L); years_to_days(n) < dn;) {
       n++;                          /* 146097 == years_to_days(400) */
    }
    *yrPtr = n;
    n = (dn - years_to_days(n-1));
    if ( n > 59 ) {                       /* adjust if past February */
	n += 2;
	if (isleap(*yrPtr)) {
	    n -= n > 62 ? 1 : 2;
	}
    }
    *moPtr = (n * 100 + 3007) / 3057;    /* inverse of months_to_days() */
    *dayPtr = n - months_to_days(*moPtr);
}

int
Dci_Dn2Dow(int dn)
{
    dn += DNOFF;
    return (dn % 7);
}

int
Dci_Time2Dn(time_t time)
{
    return (((int) time) / SECPERDAY);
}


static int
Date2DnCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int yr, mo, day, dn;

    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " year month day\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &yr) != TCL_OK ||
	Tcl_GetInt(interp, argv[2], &mo) != TCL_OK ||
	Tcl_GetInt(interp, argv[3], &day) != TCL_OK) {
	return TCL_ERROR;
    }
    dn = Dci_Date2Dn(yr, mo, day);
    Dci_SetIntResult(interp, dn);
    return TCL_OK;
}


static int
Dn2DateCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int yr, mo, day, dn;
    int fmt = (int) arg;
    char buf[100];

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " scalar ?fmt?\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &dn) != TCL_OK) {
	return TCL_ERROR;
    }
    Dci_Dn2Date(dn, &yr, &mo, &day);
    switch (fmt) {
    case 'p':
    	sprintf(buf, "%04d%02d%02d", yr, mo, day);
  	break;
    default:
    	sprintf(buf, "%d %d %d", yr, mo, day);
	break;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
Dn2DowCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int dn;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " scalar\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &dn) != TCL_OK) {
	return TCL_ERROR;
    }
    Dci_SetIntResult(interp, Dci_Dn2Dow(dn));
    return TCL_OK;
}


static int
Time2DnCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    time_t time;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], (int *) &time) != TCL_OK) {
	return TCL_ERROR;
    }
    Dci_SetIntResult(interp, Dci_Time2Dn(time));
    return TCL_OK;
}
