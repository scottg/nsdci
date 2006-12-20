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

#define isLeap(y) ((!(y % 4) && (y % 100)) || !(y % 400))

static Tcl_CmdProc StrptimeCmd, StrftimeCmd, Week2GregorianCmd, GetWeekCmd;

void
DciTimeLibInit(void)
{
    DciAddIdent(rcsid);
}

int
DciTimeTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.strptime", StrptimeCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.strftime", StrftimeCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.strftimef", StrftimeCmd, (ClientData) 'f',
		      NULL);
    Tcl_CreateCommand(interp, "dci.strftime2", StrftimeCmd, (ClientData) 'g',
		      NULL);
    Tcl_CreateCommand(interp, "log.week2date", Week2GregorianCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.week2date", Week2GregorianCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "log.getWeek", GetWeekCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.getWeek", GetWeekCmd, NULL, NULL);
    return TCL_OK;
}


static int
StrptimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char		*fmt, *string;
    char		*res;
    struct tm	tm;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?format? string\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 2) {
    	fmt = "%c";
    } else {
		fmt = argv[1];
    }
    string = argv[argc-1];
    res = strptime(string, fmt, &tm);
    if (res == NULL || res == string) {
		Tcl_AppendResult(interp, "invalid format \"",
            fmt, "\" for date \"", string, "\"", NULL);
        return TCL_ERROR;
    }
	/* NB: Set dst to -1 to avoid overcompensation. */
	tm.tm_isdst = -1;
    Dci_SetIntResult(interp, (int) mktime(&tm));
    return TCL_OK;
}


static int
StrftimeCmd(ClientData arg,Tcl_Interp *interp, int argc, char **argv)
{
    double d;
    time_t time;
    char *fmt, buf[200];
    size_t res;
    int cmd = (int) arg;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?format? time\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetDouble(interp, argv[argc-1], &d) != TCL_OK) {
	return TCL_ERROR;
    }
    time = (time_t) d;
    if (argc == 3) {
	fmt = argv[1];
    } else {
	fmt = cmd == 'f' ? "%c" : "%KC";
    }
    switch (cmd) {
    case 'f':
    	res = Dci_Strftime(buf, sizeof(buf), fmt, time);
	break;
    case 'g':
        res = strftime(buf, sizeof(buf), fmt, ns_gmtime(&time));
	break;
    default:
    	res = strftime(buf, sizeof(buf), fmt, ns_localtime(&time));
	break;
    }
    if (res == 0) {
	Tcl_SetResult(interp, "could not format time", TCL_STATIC);
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetWeekCmd --
 *
 *      Converts Gregorian date to ISO 8601 week.
 *      NB: Year YYYY, Month 1-12, Day 1-31.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      none.
 *
 *----------------------------------------------------------------------
 */

static int
GetWeekCmd(dummy, interp, argc, argv)
    ClientData dummy;   /* Not used. */
    Tcl_Interp *interp; /* Current interpreter. */
    int argc;           /* Number of arguments. */
    char **argv;        /* Argument strings. */
{
    int year, month, day, leapYr, lastYrLeap, dayOfYr;
    int jan1WkDay, yy, c, g, wkDay, yrNum, wkNum;
    int Month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    
    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " year month day\"", NULL);
        return TCL_ERROR;
    }
    
    if (Tcl_GetInt(interp, argv[1], &year) != TCL_OK
            || Tcl_GetInt(interp, argv[2], &month) != TCL_OK
            || Tcl_GetInt(interp, argv[3], &day) != TCL_OK) {
        return TCL_ERROR;
    }
    
    leapYr = isLeap(year);
    lastYrLeap = isLeap(year - 1);
    dayOfYr = day + Month[month-1];
    
    if (leapYr && month > 2) {
        dayOfYr += 1;
    }
    
    yy = (year - 1) % 100;
    c = (year - 1) - yy;
    g = yy + yy/4;
    jan1WkDay = 1 + (((((c / 100) % 4) * 5) + g) % 7);
    wkDay = 1 + (((dayOfYr + (jan1WkDay - 1)) - 1) % 7);
    wkNum = 1; /* default, to quiet compiler */
    if (dayOfYr <= (8 - jan1WkDay) && jan1WkDay > 4) {
        yrNum = year - 1;
        if (jan1WkDay == 5 || (jan1WkDay == 6 && lastYrLeap)) {
            wkNum = 53;
        } else {
            wkNum = 52;
        }
    }
    
    if (((leapYr ? 366 : 365) - dayOfYr) < (4 - wkDay)) {
        yrNum = year + 1;
        wkNum = 1;
    } else {
        yrNum = year;
    }
    
    if (yrNum == year) {
        wkNum = (dayOfYr + (7 - wkDay) + (jan1WkDay - 1)) / 7;
        if (jan1WkDay > 4 && !(leapYr && jan1WkDay == 7)) {
            wkNum -= 1;
        }
    }
    
    Dci_SetIntResult(interp, wkNum);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Week2GregorianCmd --
 *
 *      Converts ISO 8601 week date to Gregorian date.
 *      NB: Year YYYY, Month 1-12, Day 1-31.
 *
 * Results:
 *      Standard Tcl return code.
 *
 * Side effects:
 *      none.
 *
 *----------------------------------------------------------------------
 */

static int
Week2GregorianCmd(dummy, interp, argc, argv)
    ClientData dummy;   /* Not used. */
    Tcl_Interp *interp; /* Current interpreter. */
    int argc;           /* Number of arguments. */
    char **argv;        /* Argument strings. */
{
    int year, week, weekday = 1;
    int yy, c, g, jan1WkDay, gYear, gMonth, gDay, dayOfYr, H, i;
    int Month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    char buf[100];

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " year week ?weekday?\"", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[1], &year) != TCL_OK
            || Tcl_GetInt(interp, argv[2], &week) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argc == 4 && Tcl_GetInt(interp, argv[3], &weekday) != TCL_OK) {
        return TCL_ERROR;
    }
    
    yy = (year - 1) % 100;
    c = (year - 1) - yy;
    g = yy + yy/4;
    
    jan1WkDay = 1 + (((((c / 100) % 4) * 5) + g) % 7);
    gMonth = 1; /* default, to quiet compiler */
    gDay = 1; /* default... */
    if (week == 1 && jan1WkDay < 5 && weekday < jan1WkDay) {
        gYear = year - 1;
        gMonth = 12;
        gDay = 32 - (jan1WkDay - weekday);
        goto done;
    } else {
        gYear = year;
    }
    
    dayOfYr = (week - 1) * 7;
    
    if (jan1WkDay < 5) {
        dayOfYr += weekday - (jan1WkDay - 1);
    } else {
        dayOfYr += weekday + (8 - jan1WkDay);
    }
    if (dayOfYr > (isLeap(year) ? 366 : 365)) {
        gYear = year + 1;
        gMonth = 1;
        gDay = dayOfYr - (isLeap(year) ? 366 : 365);
    } else {
        gYear = year;
    }
    
    if (gYear == year) {
        if (isLeap(year)) {
            Month[1] = 29;
        }
        H = 0;
        for (i = 0; H < dayOfYr; i++) {
            H += Month[i];
        }
        gMonth = i;
        gDay = dayOfYr - (H - Month[i-1]);
    }
    
done:
    sprintf(buf, "%d %d %d", gYear, gMonth, gDay);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}
