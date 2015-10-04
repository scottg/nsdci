# Network Eval (neval) #
nsdci/dcicommon/neval.c

## Introduction ##
The Network Eval API (neval) is used to evaluate a script on a remote host.  No result is returned.

## TCL API ##
```
--------------------------------------------------------------------------------
```
### neval.send ###
Sends a script or command to a remote host. No result is returned.
**neval.send** _host port pass script ?timeout?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _host_       | String. The IP address of the remote client. |
| _port_       | String. The port configured for neval on the remote client. |
| _pass_       | String. The neval password configured on the remote client. |
| _script_     | String. The script or command to evaluate. |
| _?timeout?_  | Integer. The time in seconds to wait brefore timing out. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The script was sent. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

## Configuration ##
#### Client: ####
```
    ns_section "ns/server/$server/module/dci/neval"
        ns_param address $address
        ns_param port $nevalPort
        ns_param password $encryptedPassword ; # ns_crypt $password t2
        ns_param allowed 127.0.0.1
```
## Usage ##
In the tool [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to send a command to the frontend server (See [Topology](Topology.md) for more detail):
```
    % telnet 127.0.0.1 8900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password: 

    Welcome to tool running at /usr/local/aolserver/bin/nsd (pid 6930)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    tool:nscp 1> neval.send 127.0.0.1 9600 neval "ns_log notice foo"

    tool:nscp 2> exit

    Goodbye!
    Connection closed by foreign host.
```

Looking at the tool server log you will see the following log entry:

```
    [07/Jan/2007:18:10:43][6930.25205760][-nscp:4-] Notice: neval: send to 127.0.0.1:9600: {ns_log notice foo}
```

Looking in the frontend server log you will the connection as well as the result of the command since it was a log notice:

```
    [07/Jan/2007:18:10:43][6928.25189376][-socks-] Notice: neval: connected to 127.0.0.1
    [07/Jan/2007:18:10:43][6928.25189376][-socks-] Notice: neval: queued 127.0.0.1
    [07/Jan/2007:18:10:43][6928.25208832][-neval-] Notice: recv from 127.0.0.1: {ns_log notice foo}
    [07/Jan/2007:18:10:43][6928.25208832][-neval-] Notice: foo

```

## Best Practices ##