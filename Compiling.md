# File System Layout #
```
~shmooved/
    cvs/tcl/          - Tcl source
    cvs/aolserver/    - AOLserver source
    svn/nsdci/        - nsdci source

/usr/local/aolserver  - Build directory
```
# Download and Compile Tcl (8.4.14) #
```
% cd ~shmooved/cvs
% cvs -d:pserver:anonymous@tcl.cvs.sourceforge.net:/cvsroot/tcl login
% cvs -z3 -d:pserver:anonymous@tcl.cvs.sourceforge.net:/cvsroot/tcl co -P -rcore-8-4-14 tcl
% cd tcl/unix
%./configure --prefix=/usr/local/aolserver --enable-symbols --enable-threads
% make
% make install
```
# Download and Compile AOLserver (HEAD - 4.5) #
```
% cd ~shmooved/cvs
% cvs -d:pserver:anonymous@aolserver.cvs.sourceforge.net:/cvsroot/aolserver login
% cvs -z3 -d:pserver:anonymous@aolserver.cvs.sourceforge.net:/cvsroot/aolserver co -P aolserver
% cd aolserver
% ./configure --enable-symbols --prefix=/usr/local/aolserver --with-tcl=/usr/local/aolserver/lib
% /usr/local/aolserver/bin/tclsh8.4 nsconfig.tcl -debug -install /usr/local/aolserver
% make
% make install
```
# Download and Compile nsdci (HEAD) #
```
% cd ~shmooved/svn
% svn checkout http://nsdci.googlecode.com/svn/trunk/ nsdci
```
## Compile GDBM ##
```
% cd ~shmooved/svn/nsdci/gdbm-1.8.3
% ./configure --enable-symbols --prefix=/usr/local/aolserver
% vi Makefile

    17 # File ownership and group
    18 BINOWN = shmooved
    19 BINGRP = shmooved
    
% make
% make install
```
## Compile dcicommon ##
```
% cd ~shmooved/svn/nsdci/dcicommon
% make
% make install
```
## Compile dciadmin ##
```
% cd ~shmooved/svn/nsdci/dciadmin
% make
% make install
```