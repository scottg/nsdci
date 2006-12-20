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

static Tcl_CmdProc HrefAppendCmd;

 
void
DciHtmlLibInit(void)
{
    DciAddIdent(rcsid);
}

int
DciHtmlTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "html.hrefAppendRedir", HrefAppendCmd, NULL,
		      NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HrefAppendCmd --
 *
 *      Parses htmlchunk and replaces a href's with a redirect page
 *      and formdata the caller specifies.
 *      *** htmlchunk - any chunk of html that needs to have href's redir'd
 *      *** formdata  - extra formdata to pass to redir. i.e. "spot=c1.home.main"
 *      *** redirpage - redirect page to send these links to. i.e. "/redir.adp"
 *
 * Results:
 *      returns htmlchunk with href's replaced with redirect.
 *
 *----------------------------------------------------------------------
 */

static int
HrefAppendCmd(ClientData arg,Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString url;
    char *p, *s, *e, *ch, *sp, *stag, \
         *etag, *xtags;
    int  len;
 
    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " htmlchunk formdata redirpage \"", NULL);
        return TCL_ERROR;
    }
 
    xtags = "";
    stag  = "<a href=";
    etag  = ">";
    p     = argv[1];
 
    while ((s = strstr(p, stag)) && (e = strstr(s, etag))) {
        Ns_DStringInit(&url);
        ch  = s;
        s   = s+8; /* length of stag*/
        *ch = *e = '\0';

        /* looking for existance of extra tags i.e. "target=_blank"*/
        if ((sp = (strstr(s, " ")))) {
            *sp = '\0';
            xtags = sp+1;
        } else {
            xtags = "";
        }

        len = strlen(s); 
        /* checking for single and double quotes, then pulling them out */
        if ((len >= 2) && \
            (((s[0] == '\"') && (s[len-1] == '\"')) ||
             ((s[0] == '\'') && (s[len-1] == '\'')))) {
            s[len-1] = '\0';
            s++;
        }

        Ns_EncodeUrl(&url, s);
        Tcl_AppendResult(interp, p, stag, "\"", argv[3], "?", argv[2], \
                         "&url=", url.string, "\"", NULL);
        /* if we have extra tags, put them in */
	if (strlen(xtags)) {
	    Tcl_AppendResult(interp, " ", xtags, NULL);
	}
        Tcl_AppendResult(interp, etag, NULL);

        Ns_DStringFree(&url);
        p = e+1;
    }
 
    Tcl_AppendResult(interp, p, NULL);
 
    return TCL_OK;
}
