# Networked Proxy (nproxy) #
nsdci/dcicommon/nproxy.c

## Introduction ##
The Networked Proxy API (nproxy) is used to send scripts or commands to remote servers for execution.  The result is sent back to the client.

Additional Reading: [rpc](rpc.md)

## TCL API ##
```
--------------------------------------------------------------------------------
```
### nproxy.send ###
Used to send scripts or commands to a named nproxy.  See the "Configuration" section for more information.

**nproxy.send** _server script ?timeout?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _server_     | String. A named nproxy server. See the "Configuration" section for more information. |
| _script_     | String. The script or command to send. |
| _?timeout?_  | Integer. Optional. How long to wait for the result. |


| **Result** | Description |
|:-----------|:------------|
| `String`   | Success. The result of script or command. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nproxy.debug ###
Used to retrieve the current debug value, or to set a new debug value.

**nt.proxy** _?boolean?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _?boolean?_  | Boolean. Optional. Specifies whether or not debugging is enabled. |


| **Result** | **Description** |
|:-----------|:----------------|
| `1`        | Success. Returned if a boolean was specified. |
| `BOOLEAN`  | Success. If a boolean was not specified, then the current debug value is returned. |

```
--------------------------------------------------------------------------------
```

## Configuration ##
#### Server: ####
```
    ns_section "ns/server/$server/modules"
        ns_param nssock nssock.so
        ns_param dci dci.so

    ns_section "ns/server/$server/module/nssock"
        ns_param hostname $hostName
        ns_param address $address
        ns_param port $httpPort

    ns_section "ns/server/$server/module/dci/rpc/server/nproxy"
        ns_param http 1

    ns_section "ns/server/$server/module/dci/rpc/server/nproxy/acl"
        ns_param allow 127.0.0.1

    ns_section "ns/server/$server/module/dci/nproxy"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/nproxy/servers"
        ns_param $nproxyServerName $nproxyServerName

    ns_section "ns/server/$server/module/dci/nproxy/server/$nproxyServerName"
        ns_param handshakeName $nproxyServerName

    ns_section "ns/server/$server/module/dci/nproxy/server/$nproxyServerName/clients"
        # This section has no params but is required.
        # Legacy client list before HTTP RPC.
        # See the rpc/server acl section above. 
```
#### Client: ####
```
    ns_section "ns/server/$server/module/dci/rpc/client/nproxy:${$nproxyServerName}"
        ns_param address $nproxyServerAddress
        ns_param port $nproxyServerPort
        ns_param httpkeepalive true
        ns_param httpnumconnections 1

    ns_section "ns/server/$server/module/dci/nproxy"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/nproxy/clients"
        ns_param $nproxyServerName $nproxyServerName

    ns_section "ns/server/$server/module/dci/nproxy/client/$nproxyServerName"
        ns_param timeout 1
```

## Usage ##
Use the frontend [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to send a command to the tool server (See [Topology](Topology.md) for more information):
```
    % telnet 127.0.0.1 9900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password: 

    Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 7893)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    frontend:nscp 1> nproxy.send tool "expr 2 + 2"
    4
    frontend:nscp 2> 
```
If you tail the tool server log you will see the following notice:
```
    [08/Jan/2007:15:28:54][7884.25210880][-conn:0-] Notice: eval: 0 expr 2 + 2 ...
```
The tool server accepted the request and evaluated the script. The result was sent back to the frontend server and was seen in the frontend AOLserver Control Port.  Send a log notice to the tool server (See [Topology](Topology.md) for more information):
```
    frontend:nscp 2> nproxy.send tool {ns_log notice "Sent this from the frontend server."}

    frontend:nscp 3> exit

    Goodbye!
    Connection closed by foreign host.
```
There is no returned result for a log notice. But if you look in the tool server log you will see the evaluation notice as well as the result of the script:
```
    [08/Jan/2007:15:30:08][7884.25210880][-conn:0-] Notice: eval: 0 ns_log notice "Sent this from the fronte ...
    [08/Jan/2007:15:30:08][7884.25210880][-conn:0-] Notice: Sent this from the frontend server.
```