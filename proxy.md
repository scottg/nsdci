# Proxy (proxy) #
nsdci/dcicommon/proxy.c

## Introduction ##
The Proxy API (proxy) manages modified tcl shells capable of communicating with AOLserver over pipes. These processes are created on demand and maintained in pools. Each pool can source an init script which could extend the proxy with tcl procedures. The Proxy API is useful for isolating code that is not thread-safe, executing code that changes environmental variables, or creating a forkable light-weght process.

## TCL API ##
```
--------------------------------------------------------------------------------
```
### proxy.start ###
Used to create a named proxy pool.  Processes are initialized on demand, not as a result of this command.

**proxy.start** _name num ?init?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _name_       | String. The name of the proxy pool. |
| _num_        | Integer. The number of proxy processes available to the pool. |
| _?init?_     | String. Optional. The name of a script to source when a proxy process is initialized. |


| **Result** | **Description** |
|:-----------|:----------------|
| `VOID`     | Success. The pool was created. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### proxy.send ###
Send a script to a proxy process for execution. If there are no processes from the pool initialized and idle, a new  process will initialize.

**proxy.send** _pool script ?timeout?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _name_       | String. The name of the proxy pool. |
| _script_     | String. The script to be evaluated by the proxy process. |
| _?timeout?_  | Integer. The time to wait before timing out. |


| **Result** | **Description** |
|:-----------|:----------------|
| `String`   | Success. The result of the script. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

| **Side Effects** |
|:-----------------|
| If there are no processes from the pool initialized and idle, a new process will initialize. |

```
--------------------------------------------------------------------------------
```

### proxy.pools ###
Lists all the proxy pools by name.

**proxy.pools**

| **Result** | **Description** |
|:-----------|:----------------|
| `TCL_LIST` | A list of proxy pool names. |

```
--------------------------------------------------------------------------------
```

## Usage ##
Create a script called /proxy.cmds with the following tcl procedure:

```
    proc execPs {} {
        return [exec ps]
    }
```

The procedure execs the UNIX _ps_ command.  Depending on the process size of our AOLserver, we might not want that to be executed inside the AOLserver process space.  Instead we will create a proxy pool and source the procedure there. Use the tool [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to create the proxy pool and issue the execPs command (see [Topology](Topology.md) for more detail):

```
    [neon:~] Michael% telnet 127.0.0.1 8900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password:

    Welcome to tool running at /usr/local/aolserver/bin/nsd (pid 7231)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    tool:nscp 1> proxy.start myProxyPool 5 /proxy.cmds

    tool:nscp 2> proxy.send myProxyPool execPs
     PID  TT  STAT      TIME COMMAND
     7193  p1  S      0:00.05 -tcsh
     7239  p1  S+     0:00.01 telnet 127.0.0.1 8900
     6145  p3  S      0:00.05 -tcsh
     7231  p3  S+     0:00.13 ./bin/nsd -ft ./etc/tool.tcl
     7240  p3  S+     0:00.02 /usr/local/aolserver/bin/dcitclx -P myProxyPool /prox
     6197  p5  S      0:00.04 -tcsh
     6929  p5  S+     0:00.07 ./bin/nsd -ft ./etc/sob.tcl
     6202  p6  S      0:00.04 -tcsh
     6928  p6  S+     0:00.15 ./bin/nsd -ft ./etc/frontend.tcl
    tool:nscp 3>
```

The first command created the proxy pool with a max of 5 processes.  These processes will initialize on demand, so when we use _proxy.send_ for the first time, it will initialize a process, execute the command in the process, and return the result.  We see the following log entries in the server log when a proxy process is started:

```
    [07/Jan/2007:19:26:24][7231.25210880][-nscp:2-] Notice: proxy[myProxyPool]: starting /usr/local/aolserver/bin/dcitclx /proxy.cmds
    [07/Jan/2007:19:26:24][7240.2684407744][dcitclx:proxy] Notice: starting
    [07/Jan/2007:19:26:24][7240.2684407744][dcitclx:proxy] Notice: sourced: /proxy.cmds
    [07/Jan/2007:19:26:24][7240.2684407744][dcitclx:proxy] Notice: running
```

There is now a process initialized and idle, so there is no need to initialize another one from the pool when the command issued again. Only if all the currently initialized processes are in use will another one be initialized from the pool.  If the max number of processes available to a pool are all busy, the proxy.send command will wait until the specified timeout value and then timeout.

## Best Practices ##
There is a performance hit when a proxy starts. You might consider starting at least one proxy from your pool at server start-up. This will ensure there is at least one proxy initialized when needed.