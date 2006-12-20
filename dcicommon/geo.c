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
#include <math.h>

static char rcsid[] = "$Id$";

static double pi;
static double longDistance[181];
static double latDistance = 69.169144;
static double radius = 3963.1;	/* NB: Assumes God's Green Earth is round. */

static Tcl_CmdProc GeoDistanceCmd, GeoBoxCmd;

void
DciGeoLibInit(void)
{
    double magic, rlat, s, c;
    int lat;

    DciAddIdent(rcsid);
    pi = atan(1.0)*4;
    magic = cos(pi/180.0);	/* NB: Same longitude magic. */
    for (lat = 0; lat < 181; ++lat) {
        rlat = lat*pi/180;
	s = sin(rlat);
	c = cos(rlat);
        longDistance[lat] = radius*acos((s*s)+(magic*c*c));
    }
}


int
DciGeoTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "geo.box", GeoBoxCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "geo.distance", GeoDistanceCmd, NULL, NULL);
    return TCL_OK;
}


static int
GeoDistanceCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    double lat1, lng1, lat2, lng2;
    double rlat, rlng, rlat2, rlng2;
    double miles;
    char buf[100];

    if (argc != 5) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " latitudeA longitudeA latitudeB longitudeB\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetDouble(interp, argv[1], &lat1) != TCL_OK
        || Tcl_GetDouble(interp, argv[2], &lng1) != TCL_OK
        || Tcl_GetDouble(interp, argv[3], &lat2) != TCL_OK
        || Tcl_GetDouble(interp, argv[4], &lng2) != TCL_OK) {
        return TCL_ERROR;
    }
    
    lat1 /= 10000.0;
    lng1 /= 10000.0;
    lat2 /= 10000.0;
    lng2 /= 10000.0;
    
    rlat = lat1*pi/180;
    rlng = lng1*pi/180;
    rlat2 = lat2*pi/180;
    rlng2 = lng2*pi/180;
    if (rlat == rlat2 && rlng == rlng2) {
	miles = 0;
    } else {
        miles = radius*acos(sin(rlat)*sin(rlat2)+cos(rlng-rlng2)*cos(rlat)*cos(rlat2));
    }
    sprintf(buf, "%f", miles);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


static int
GeoBoxCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    double ulat, ulng, llat, llng, clat, clng;
    double miles, latD, lngD;
    char buf[100];
    
    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                " cntrLatitude cntrLongitude miles\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetDouble(interp, argv[1], &clat) != TCL_OK
        || Tcl_GetDouble(interp, argv[2], &clng) != TCL_OK
        || Tcl_GetDouble(interp, argv[3], &miles) != TCL_OK) {
        return TCL_ERROR;
    }
    
    clat /= 10000.0;
    clng /= 10000.0;
    
    if (miles <= 0) {
        Tcl_SetResult(interp, "miles must be greater than zero", TCL_STATIC);
        return TCL_ERROR;
    }
    lngD = miles/longDistance[(int) fabs(clat)];
    latD = miles/latDistance;
    ulat = (clat+latD > 180 ? 180 - clat+latD : clat+latD) * 10000;
    ulng = (clng+lngD > 180 ? 180 - clng+lngD : clng+lngD) * 10000;
    llat = (clat-latD < -180 ? 180 + clat-latD : clat-latD) * 10000;
    llng = (clng-lngD < -180 ? 180 + clng-lngD : clng-lngD) * 10000;
    sprintf(buf, "%d %d %d %d", (int) ulat, (int) ulng, 
            (int) llat, (int) llng);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}
