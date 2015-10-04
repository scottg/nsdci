# Networked Variable (nv) #
nsdci/dcicommon/nv.c

## Introduction ##
The Networked Variable api (nv) is used to stream key-value updates to remote clients. It is an in-memory array on multiple servers.  The array is networked, so array manipulation on the origin server is reflected on the clients. The array has the option to be flushed to disk for persistent storage across restarts.  Clients will receive the entire array from the origin server on startup.

Additional Reading: [broadcaster](broadcaster.md), [receiver](receiver.md).

## TCL API ##
```
--------------------------------------------------------------------------------
```
nv.exists nv.incr nv.set nv.file nv.tget nv.stats nv.names nv.setSleep nv.lappend nv.get nv.dump nv.debug nv.listen nv.flush nv.append nv.unset nv.cset nv.arrays nv.create nv.load nv.connect nv.cget

## Configuration ##
#### Server: ####
Create the nv array, create the nv server, specify which arrays the nv server can serve, then configure the clients:
```
    ns_section "ns/server/$server/module/dci/nv"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/nv/arrays"
        ns_param nv nv

    ns_section "ns/server/$server/module/dci/nv/array/nv"
        ns_param persist 1

    ns_section "ns/server/$server/module/dci/nv/servers"
        ns_param $nvServerName $nvServerName

    ns_section "ns/server/$server/module/dci/nv/server/$nvServerName"
        ns_param login "nv"

    ns_section "ns/server/$server/module/dci/nv/server/$nvServerName/arrays" 
        ns_param nv nv

    ns_section "ns/server/$server/module/dci/nv/server/$nvServerName/clients"
        ns_param $nvClientName $clientAddress:$clientReceiverPort
```

#### Client: ####
Create the nv array, create the nv client, then specify which arrays the nv client can receive.
```
    ns_section "ns/server/$server/module/dci/nv"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/nv/arrays"
        ns_param nv nv

    ns_section "ns/server/$server/module/dci/nv/array/nv"
        ns_param persist 1

    ns_section "ns/server/$server/module/dci/nv/clients"
        ns_param $nvClientName $nvClientName

    ns_section "ns/server/$server/module/dci/nv/client/$nvClientName"
        ns_param login "nv"

    ns_section "ns/server/$server/module/dci/nv/client/$nvClientName/arrays"
        ns_param nv nv
```
## Usage ##
The bundled set up comes with a pre-configured nv array called "nv" with the frontend server as a client (See [Topology](Topology.md) for more detail). In the tool [Control Port](http://code.google.com/p/aolserver/wiki/nscp), set some nv keys:
```
    % telnet 127.0.0.1 8900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login:
    Password:

    Welcome to tool running at /usr/local/aolserver/bin/nsd (pid 6799)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    tool:nscp 1> nv.set nv foo bar
    bar
    tool:nscp 2> nv.set nv myKey "some value"
    some value
    tool:nscp 3> exit

    Goodbye!
    Connection closed by foreign host.
```

With debugging turned on, in the frontend server log you will see the updates arrive and the array flush to disk:

```
    [07/Jan/2007:14:55:20][6801.25189376][-socks-] Notice: nvc[frontend]: array: nv = nv
    [07/Jan/2007:14:55:20][6801.25189376][-socks-] Notice: nvc[frontend]: set nv[foo] = bar
    [07/Jan/2007:14:55:40][6801.25190400][-nvf-] Notice: nvf[nv]: flushed
    [07/Jan/2007:14:55:41][6801.25189376][-socks-] Notice: nvc[frontend]: array: nv = nv
    [07/Jan/2007:14:55:41][6801.25189376][-socks-] Notice: nvc[frontend]: set nv[myKey] = some value
    [07/Jan/2007:14:56:10][6801.25190400][-nvf-] Notice: nvf[nv]: flushed
```

Similar flush entries can bee seen in the tool's server log:

```
    [07/Jan/2007:14:55:36][6799.25191424][-nvf-] Notice: nvf[nv]: flushed
    [07/Jan/2007:14:56:06][6799.25191424][-nvf-] Notice: nvf[nv]: flushed
```

In the frontend AOLserver Control Port read the keys that were set by the tool server:

```
    [neon:local/aolserver/etc] Michael% telnet 127.0.0.1 9900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login:
    Password:

    Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 6801)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    frontend:nscp 1> nv.get nv foo
    bar
    frontend:nscp 2> nv.get nv myKey
    some value
    frontend:nscp 3> nv.dump nv
    foo bar fo bar myKey {some value}
    frontend:nscp 4> exit

    Goodbye!
    Connection closed by foreign host.
    [neon:local/aolserver/etc] Michael%
```

## Best Practices ##
Since clients receive the entire array on startup, attention should be paid to the number of nv clients an origin server has, the size of those arrays, and how many arrays each client has connected.  The combination of number and size can decrease performance on a cold start.

Try to keep nv arrays bounded. Unbounded arrays will grow over time and cause the performance issues described above.