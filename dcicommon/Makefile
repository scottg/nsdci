# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
#
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.

AOLSERVER ?= /usr/local/aolserver
GDBM     = $(AOLSERVER)
CFLAGS   = -I$(GDBM)/include
DCI_H	 = dci.h
HDRS	 = $(DCI_H) dciInt.h
DLL      =  dci
DLLINIT  =  DciLibInit
DLLLIBS  += -lgdbm 
OBJS	 =  \
	agf.o art.o ascii85.o av.o base64.o brange.o	\
	broadcast.o cache.o cb.o cbapi.o cfg.o cmdinfo.o compat30.o	\
	datenum.o eval.o file.o flush.o gdbm.o geo.o html.o http.o	\
	init.o img.o list.o log.o main.o misc.o netdb.o	\
	neval.o nfs.o nproxy.o nrate.o nv.o otally.o page.o parse.o poll.o	\
	proxy.o receiver.o rpc.o server.o sl.o sob.o	\
	sock.o strftime.o tclexport.o time.o unix.o uuid.o modinit.o

MAN	 =  dci.n
MOD      =  dci
MODINIT  =  Dci_ModInit
PGM      =  dcitclx
PGMOBJS	 =  dcitcl.o

INSTALL  =  install-hdr
CFLAGS   += -D_DCIBUILD

include $(AOLSERVER)/include/ns.mak

install-hdr:
	$(INST) -d $(AOLSERVER)/include -f $(DCI_H) 

install-docs:
	$(MAKEALL) install doc
