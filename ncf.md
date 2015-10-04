# Network Cache Flush (ncf) #
nsdci/dcicommon/flush.c

## Introduction ##
The Network Cache Flush api (ncf) is used to send and receive cache flush messages across a network.  It flushes named caches created by the [cache](cache.md) api.  You must set up a "ncf server" to broadcast the flush and a "ncf client" to receive the flush. The api is only exposed when properly configured.  See the "Configuration" section for more information.

Additional Reading: [broadcaster](broadcaster.md), [receiver](receiver.md), [nsob](nsob.md).

## TCL API ##
```
--------------------------------------------------------------------------------
```
### ncf.addFlushCache ###
Used to register named caches on the client for flush-on-connect. The entire cache will be flushed. This is useful if there are n-tier ncf repeaters in the topology. If a ncf repeater was down, it may have not forwarded ncf messages to its clients. This will ensure out of date cache entries are not used by the client after a reconnect. This is default behavior for clients connected to ncf repeaters.

**ncf.addFlushCache** _cache_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cache_      | String. The named cache to be registered. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The cache was registered. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### ncf.debug ###
Used to retrieve the current debug value, or to set a new debug value.

**ncf.debug** _boolean_

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

### ncf.flushOnConnect ###
Deprecated. See _ncf.addFlushCache_.

```
--------------------------------------------------------------------------------
```

### ncf.send ###
Sends a cache flush message to all configured clients.

**ncf.send** _cache key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cache_      | String. The named cache created by the [cache](cache.md) api. |
| _key_        | String. The key in the named cache. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The flush message was sent. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### ncf.sendAll ###
Sends a cache flush message to all configured clients. Flushes all keys in the cache.

**ncf.sendAll** _cache_

| **Argument** | **Description** |
|:-------------|:----------------|
| _cache_      | String. The named cache created by the [cache](cache.md) api. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The flush message was sent. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

## Configuration ##
#### Server: ####
```
    ns_section "ns/server/$server/module/dci/broadcast"
        ns_param debug 1
        ns_param timeoutinterval 2

    ns_section "ns/server/$server/module/dci/ncf/clients"
        ns_param frontend $frontendAddress:$frontendReceiverPort
        ns_param tool $toolAddress:$toolReceiverPort

    ns_section "ns/server/$server/module/dci/ncf"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/nfs"
        ns_param debug 1
```
#### Client: ####
```
    ns_section "ns/server/$server/module/dci/receiver"
        ns_param address $address
        ns_param port $receiverPort

    ns_section "ns/server/$server/module/dci/ncf"
        ns_param debug 1
        ns_param client 1
```

## Usage ##
Use the sob [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to turn on debugging and send a cache flush (See [Topology](Topology.md) for more information):
```
    % telnet 127.0.0.1 7900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login:
    Password:
    Welcome to sob running at /usr/local/aolserver/bin/nsd (pid 6203)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    sob:nscp 1> ncf.debug 1
    1
    sob:nscp 2> ncf.send foo bar
    sob:nscp 3> exit
    Goodbye!
    Connection closed by foreign host.
```

You can see by the following log entries that the message was sent to all the configured ncf clients:

```
    [07/Jan/2007:12:51:59][6203.25191424][-nscp:2-] Notice: ncf: queue for send: foo[bar]
    [07/Jan/2007:12:51:59][6203.25190400][broadcast:ncf] Notice: frontend: send complete
    [07/Jan/2007:12:51:59][6203.25190400][broadcast:ncf] Notice: tool: send complete
```

Turn on debugging on a client, and you will see the following log entries:

```
    [07/Jan/2007:12:51:59][6288.25189376][-socks-] Notice: ncf: received flush foo[bar]
    [07/Jan/2007:12:51:59][6288.25189376][-socks-] Notice: ncf: no such cache: foo
```

The client received the message but does not have a named cache called "foo" so the message is ignored.