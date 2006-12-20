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

static Tcl_CmdProc ChmodCmd, FsyncCmd, FtruncateCmd, LinkCmd, LockFpCmd,
	MkdirCmd, ReadFileCmd, RealpathCmd, RenameCmd, RmCmd, RmdirCmd,
	SymlinkCmd, TruncateCmd, UnlinkCmd, UtimeCmd, WriteFile2Cmd,
	WriteFileCmd;


void
DciUnixLibInit(void)
{
    DciAddIdent(rcsid);
}


int
DciUnixTclInit(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "dci.chmod", ChmodCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.mkdir", MkdirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.unlink", UnlinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rm", RmCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rename", RenameCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.truncate", TruncateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.ftruncate", FtruncateCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.rmdir", RmdirCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.fsync", FsyncCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.realpath", RealpathCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.writefile", WriteFileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.appendfile", WriteFileCmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.readfile", ReadFileCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.readFile", ReadFileCmd, (ClientData) 1, NULL);
    Tcl_CreateCommand(interp, "dci.writeFile", WriteFile2Cmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.appendFile", WriteFile2Cmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.atime", UtimeCmd, (ClientData) 'a', NULL);
    Tcl_CreateCommand(interp, "dci.mtime", UtimeCmd, (ClientData) 'm', NULL);
    Tcl_CreateCommand(interp, "dci.link", LinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.symlink", SymlinkCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "dci.lockfp", LockFpCmd, NULL, NULL);
    return TCL_OK;
}


int
Dci_MkDirs(char *dir, int mode)
{
    Ns_DString ds;
    register char *p;
    int status;

    /*
     * Check if the dir already exists.
     */

    if (access(dir, F_OK) == 0) {
	return NS_OK;
    }
    if (errno != ENOENT) {
	return NS_ERROR;
    }

    /*
     * Create the directory components.
     */

    status = NS_ERROR;
    Ns_DStringInit(&ds);
    dir = Ns_NormalizePath(&ds, dir);
    p = dir;
    while (*p == '/') {
	++p;
    }
    do {
	p = strchr(p, '/');
	if (p != NULL) {
	    *p = '\0';
	}
    	if (access(dir, F_OK) != 0 && mkdir(dir, (mode_t)mode) != 0 && errno != EEXIST) {
	    goto done;
	}
	if (p != NULL) {
	    *p++ = '/';
	}
    } while (p != NULL);
    status = NS_OK;

done:
    Ns_DStringFree(&ds);
    return status;
}


static int
ChmodCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int mode;
    
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " file mode\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (chmod(argv[1], (mode_t)mode) != 0) {
        Tcl_AppendResult(interp, "chmod (\"", argv[1], "\", ", argv[2],
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


static int
MkdirCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    mode_t mode = 0755;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?-parent? directory\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
	if (mkdir(argv[1], mode) != 0) {
            Tcl_AppendResult(interp, "mkdir (\"", argv[1],
            	"\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
	}
    } else {
    	if (!STREQ(argv[1], "-parent")) {
    	    Tcl_AppendResult(interp, "unknown flag \"",
    		argv[1], "\": should be -parent", NULL);
    	    return TCL_ERROR;
    	}
	if (Dci_MkDirs(argv[2], (int)mode) != NS_OK) {
            Tcl_AppendResult(interp, "mkdir (\"", argv[2],
                    "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
	}
    }
    return TCL_OK;
}


static int
RmdirCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " directory\"", NULL);
	return TCL_ERROR;
    }
    if (rmdir(argv[1]) != 0) {
        Tcl_AppendResult(interp, "rmdir (\"", argv[1],
            "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


static int
UnlinkCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?-nocomplain? file\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3 && !STREQ(argv[1], "-nocomplain")) {
	Tcl_AppendResult(interp, "unknown flag \"",
    	    argv[1], "\": should be -nocomplain", NULL);
    	return TCL_ERROR;
    }
    if (unlink(argv[argc-1]) != 0) {
	if (argc == 2 || errno != ENOENT) {
            Tcl_AppendResult(interp, "unlink (\"", argv[argc-1],
            	"\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
	}
    }
    return TCL_OK;
}


#define DOTDIR(d) (STREQ((d), ".") || STREQ((d), ".."))

typedef struct {
    Tcl_DString	ds;
    struct stat st;
    int nocomplain;
    int recursive;
} RmCtx;


static int
Rm(Tcl_Interp *interp, RmCtx *prc)
{
    if (lstat(prc->ds.string, &prc->st) != 0) {
    	Tcl_AppendResult(interp, "lstat (\"", prc->ds.string,
            "\") failed:  ", Tcl_PosixError(interp), NULL);
    	return TCL_ERROR;
    }
    if (!S_ISDIR(prc->st.st_mode)) {
    	if (unlink(prc->ds.string) != 0) {
    	    if (!prc->nocomplain || errno != ENOENT) {
    	    	Tcl_AppendResult(interp, "unlink (\"", prc->ds.string,
    	    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
    	    	return TCL_ERROR;
    	    }
    	}
    } else {
    	if (prc->recursive) {
    	    DIR *dp;
	    int code, len;
	    struct dirent *ent;

    	    dp = opendir(prc->ds.string);
    	    if (dp == NULL) {
    		Tcl_AppendResult(interp, "opendir (\"", prc->ds.string,
    	    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
    		return TCL_ERROR;
    	    }
    	    len = prc->ds.length;
    	    code = TCL_OK;
    	    while (code == TCL_OK && (ent = ns_readdir(dp)) != NULL) {
    	    	if (!DOTDIR(ent->d_name)) {
    	    	    Tcl_DStringAppend(&prc->ds, "/", 1);
		    Tcl_DStringAppend(&prc->ds, ent->d_name, -1);
    		    code = Rm(interp, prc);
    	    	    Tcl_DStringTrunc(&prc->ds, len);
    		}
    	    }
    	    closedir(dp);
    	    if (code != TCL_OK) {
    		return TCL_ERROR;
    	    }
    	}
    	if (rmdir(prc->ds.string) != 0) {
    	    Tcl_AppendResult(interp, "rmdir (\"", prc->ds.string,
    	    	"\") failed:  ", Tcl_PosixError(interp), NULL);
    	    return TCL_ERROR;
    	}
    }
    return TCL_OK;
}


static int
RmCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    RmCtx rc;
    int i, code;
    char *end;

    if (argc < 2 || argc > 4) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?-nocomplain | -recursive? pathname\"", NULL);
	return TCL_ERROR;
    }
    rc.nocomplain = 0;
    rc.recursive = 0;
    --argc;
    for (i = 1; i < argc; ++i) {
    	if (STREQ(argv[i], "-nocomplain")) {
    	    rc.nocomplain = 1;
    	} else if (STREQ(argv[i], "-recursive")) {
    	    rc.recursive = 1;
    	} else {
    	    Tcl_AppendResult(interp, "unknown flag \"",
    	    	argv[i], "\": should be -nocomplain or -recursive", NULL);
    	    return TCL_ERROR;
    	}
    }
    Tcl_DStringInit(&rc.ds);
    Tcl_DStringAppend(&rc.ds, argv[i], -1);
    while (rc.ds.length > 0 && rc.ds.string[rc.ds.length-1] == '/') {
    	Tcl_DStringTrunc(&rc.ds, rc.ds.length-1);
    }
    end = strrchr(rc.ds.string, '/');
    if (end != NULL) {
    	++end;
    	if (DOTDIR(end)) {
    	    Tcl_AppendResult(interp, "invalid pathname \"", rc.ds.string, "\"", NULL);
    	    code = TCL_ERROR;
    	    goto done;
    	}
    }
    code = Rm(interp, &rc);
done:
    Tcl_DStringFree(&rc.ds);
    return code;
}


static int
RenameCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " old new\"", NULL);
	return TCL_ERROR;
    }
    if (rename(argv[1], argv[2]) != 0) {
        Tcl_AppendResult(interp, "rename (\"", argv[1], "\", \"", argv[2],
    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}



static int
TruncateCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int offset;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " file ?offset?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
    	offset = 0;
    } else if (Tcl_GetInt(interp, argv[2], &offset) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (truncate(argv[1], offset) != 0) {
        Tcl_AppendResult(interp, "truncate (\"", argv[1], "\", ",
            argv[2] ? argv[2] : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


static int
FtruncateCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int offset, fd;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " fileId ?offset?\"", NULL);
	return TCL_ERROR;
    }
    if (DciGetOpenFd(interp, argv[1], 1, &fd) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (argc == 2) {
    	offset = 0;
    } else if (Tcl_GetInt(interp, argv[2], &offset) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (ftruncate(fd, offset) != 0) {
        Tcl_AppendResult(interp, "ftruncate (\"", argv[1], "\", ",
            argv[2] ? argv[2] : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}
    


static int
FsyncCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int fd;
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " fileId\"", NULL);
	return TCL_ERROR;
    }
    if (DciFlush(interp, argv[1]) != TCL_OK) {
	return TCL_ERROR;
    }
    if (DciGetOpenFd(interp, argv[1], 1, &fd) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (fsync(fd) != 0) {
        Tcl_AppendResult(interp, "fsync (\"", argv[1], "\") failed:  ",
            Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int
ReadFileCmd(ClientData noent, Tcl_Interp *interp, int argc, char **argv)
{
    int fd, nread;
    struct stat st;
    char *buf;
    
    if (argc != 2) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
                argv[0], " file\"", NULL);
	    return TCL_ERROR;
    }
    if (stat(argv[1], &st) != 0) {
	if (noent && errno == ENOENT) {
	    return TCL_OK;
	}
	Tcl_AppendResult(interp, "stat (\"", argv[1],
            "\") failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    if (!S_ISREG(st.st_mode)) {
	Tcl_AppendResult(interp, "not an ordinary file: ", argv[1], NULL);
	return TCL_ERROR;
    }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
	if (noent && errno == ENOENT) {
	    return TCL_OK;
	}
	Tcl_AppendResult(interp, "couldn't open \"", argv[1],
            "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    buf = ns_malloc((size_t)(st.st_size+1));
    nread = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (nread < 0) {
	Tcl_AppendResult(interp, "read of \"", argv[1],
	    "\" failed: ", Tcl_PosixError(interp), NULL);
	ns_free(buf);
	return TCL_ERROR;
    }
    if (nread > 0 && buf[nread-1] == '\n') {
    	--nread;
    }
    buf[nread] = '\0';
    Tcl_SetResult(interp, buf, (Tcl_FreeProc *) ns_free);
    return TCL_OK;
}


static int
WriteFileCmd(ClientData type,Tcl_Interp *interp, int argc, char **argv)
{
    int fd, nwrote;
    size_t buflen;
    struct stat st;
    int flags;
    mode_t mode = 0644;
    char file[MAXPATHLEN], targetFile[MAXPATHLEN];
    char *buf, uuid[64];
    struct iovec iov[2];
    
    if (argc != 3) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
                argv[0], " file string\"", NULL);
	    return TCL_ERROR;
    }
    if ((int)type == 'a') {
        flags = (O_WRONLY|O_CREAT|O_APPEND);
    } else {
        flags = (O_WRONLY|O_CREAT|O_TRUNC);
    }
    memset(file, 0, sizeof(file));
    memset(targetFile, 0, sizeof(targetFile));
    
    /*
     * If file is a symlink, read the link and work with 
     * the actual file, not the link
     */

    if (lstat(argv[1], &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            if (realpath(argv[1], file) == NULL) {
                Tcl_AppendResult(interp, "realpath failed \"", argv[1],
                        "\": ", Tcl_PosixError(interp), NULL);
	        return TCL_ERROR;
            }
            if (stat(file, &st) != 0) {
                Tcl_AppendResult(interp, "stat failed \"", file,
                        "\": ", Tcl_PosixError(interp), NULL);
	            return TCL_ERROR;
            }
        } else {
            strcpy(file, argv[1]);
        }
        mode = st.st_mode;
    } else {
        strcpy(file, argv[1]);
    }

    /*
     * targetFile is either argv[1] or the link's readlink result,
     * which will be the target of rename.
     */

    strcpy(targetFile, file);

    /*
     * Create uniquely named temp file to write
     */

    if ((int)type != 'a') {
        strcat(file, ".");
        if (!Dci_GetUuid(uuid)) {
            Tcl_AppendResult(interp, "Dci_GetUuid: could not generate uuid", NULL);
	    return TCL_ERROR;
        }
        strcat(file, uuid); 
    }

    /* 
     * Check for file name length with uuid appended, if applicable
     */

    if (strlen(file) > MAXPATHLEN) {
        Tcl_AppendResult(interp, "file name > MAXPATHLEN \"", file,
                "\"", NULL);
	return TCL_ERROR;
    }

    fd = open(file, flags, mode);
    if (fd < 0) {
	Tcl_AppendResult(interp, "couldn't open \"", file,
            "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }

    buf = argv[2];
    buflen = strlen(buf);
    iov[0].iov_base = buf;
    iov[0].iov_len = buflen;
    iov[1].iov_base = "\n";
    iov[1].iov_len = 1;
    nwrote = writev(fd, iov, 2);
    close(fd);
    if (nwrote != (buflen + 1)) {
	Tcl_AppendResult(interp, "write (\"", argv[1],
            "\") failed: ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    
    if ((int)type != 'a') {
        if (rename(file, targetFile) < 0) {
            Tcl_AppendResult(interp, "rename (\"", targetFile,
                "\") failed: ", Tcl_PosixError(interp), NULL);
            (void) unlink(file);
            return TCL_ERROR;
        }
    }
    sprintf(file, "%d", nwrote);
    Tcl_SetResult(interp, file, TCL_VOLATILE);
    return TCL_OK;
}


static int
WriteFile2Cmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (WriteFileCmd(dummy, interp, argc, argv) != TCL_OK) {
	Ns_Log(Error, "%s: %s", argv[0], interp->result);
	Tcl_SetResult(interp, "-1", TCL_STATIC);
    }
    return TCL_OK;
}


static int
RealpathCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    char buf[MAXPATHLEN];
    
    if (argc != 2) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
                argv[0], " path\"", NULL);
	    return TCL_ERROR;
    }
    if (realpath(argv[1], buf) == NULL) {
        Tcl_AppendResult(interp, "realpath (\"", argv[1],
            "\") failed: ", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
    }
    
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    
    return TCL_OK;
}

static int
UtimeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int t;
    struct stat st;
    struct utimbuf times;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " file time\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &t) != TCL_OK) {
        return TCL_ERROR;
    }
    if (stat(argv[1], &st) != 0) {
        Tcl_AppendResult(interp, "stat failed: ", Tcl_PosixError(interp), 
            NULL);
        return TCL_ERROR;
    } 
    
    if ((int) arg == 'a') {
        times.actime = t;
        times.modtime = st.st_mtime;
    } else {
        times.actime = st.st_atime;
        times.modtime = t;
    }

    if (utime(argv[1], &times) != 0) {
        Tcl_AppendResult(interp, "utime failed: ", Tcl_PosixError(interp), 
            NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
LockFpCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int fd;
    struct flock fl;
    
    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
    	    argv[0], " fileId type\"", NULL);
    	return TCL_ERROR;
    }
    if (DciGetOpenFd(interp, argv[1], 0, &fd) != TCL_OK) {
    	return TCL_ERROR;
    }
    switch (argv[2][0]) {
    	case 'r':
    	    fl.l_type = F_RDLCK;
    	    break;
    	case 'w':
    	    fl.l_type = F_WRLCK;
    	    break;
    	case 'u':
    	    fl.l_type = F_UNLCK;
    	    break;
    	default:
    	    Tcl_AppendResult(interp, "invalid type \"",
    	    	argv[2], "\": should be read, write, or unlock", NULL);
    	    return TCL_ERROR;
    }
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    while (fcntl(fd, F_SETLKW, &fl) < 0) {
	if (errno != EINTR) {
    	    Tcl_AppendResult(interp, "fcntl(", argv[1], ", ", argv[2],
    	    	") failed: ", Tcl_PosixError(interp), NULL);
    	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}

static int
LinkCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " path1 path2\"", NULL);
	return TCL_ERROR;
    }
    if (link(argv[1], argv[2]) != 0) {
        Tcl_AppendResult(interp, "link (\"", argv[1], "\", \"", argv[2],
    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


static int
SymlinkCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " name1 name2\"", NULL);
	return TCL_ERROR;
    }
    if (symlink(argv[1], argv[2]) != 0) {
        Tcl_AppendResult(interp, "symlink (\"", argv[1], "\", \"", argv[2],
        "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}
