# File System Layout #
```
~shmooved/svn/nsdci    - nsdci source code

/usr/local/aolserver
    /etc               - configuration
    /servers/frontend  - frontend server
    /servers/tool      - tool server
    /servers/sob       - sob server
```
# Creating Directories #
```
% cd /usr/local/aolserver
% mkdir -p servers/frontend/modules
% mkdir -p servers/tool/modules
% mkdir -p servers/sob/modules
```
# Installing AOLserver Configuration Files #
```
% cd /usr/local/aolserver
% mkdir etc
% cd etc
% cp ~shmooved/svn/nsdci/config/* .
```
# Running AOLserver Instances (Foreground) #
```
% cd /usr/local/aolserver
% bin/nsd -ft etc/frontend.tcl 
% bin/nsd -ft etc/tool.tcl 
% bin/nsd -ft etc/sob.tcl 
```
# Testing #
## Frontend: SOB Write ##
```
% telnet 127.0.0.1 9900
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
login: 
Password: 

Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 14767)
AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  2 2007 at 12:32:03
CVS Tag: $Name:  $
frontend:nscp 1> nsob.put sob foo bar
```
## Frontend: SOB Read ##
```
% telnet 127.0.0.1 9900
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
login: 
Password: 

Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 14767)
AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  2 2007 at 12:32:03
CVS Tag: $Name:  $
frontend:nscp 1> nsob.get sob foo
bar
```