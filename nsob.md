# Networked Small Object Broker (nsob) #
nsdci/dcicommon/sob.c

## Introduction ##
The Networked Small Object Broker (nsob) is used to broker data between servers.  You must set up a "nsob client" to send and request data, and a "nsob server" to broker data. The api is only exposed when properly configured. See the "Configuration" section for more information.

Additional Reading: [rpc](rpc.md), [ncf](ncf.md)

## TCL API ##
```
--------------------------------------------------------------------------------
```
### nsob.put ###
Used to put data from a client to the server. If sendflush is enabled (default), a [ncf](ncf.md) message will be sent to all clients.

**nsob.put** _server key value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _server_     | String. The name of the sob server. |
| _key_        | String. The file name or key of the data. |
| _value_      | String. The data to be put. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success         |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nsob.append ###
Used to append data on the server. If sendflush is enabled (default), a [ncf](ncf.md) message will be sent to all clients.

**nsob.append** _server key value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _server_     | String. The name of the sob server. |
| _key_        | String. The file name or key of the data. |
| _value_      | String. The data to be appended. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success         |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nsob.get ###
Used to fetch data from the server. If caching is enabled (default), the API will fetch from cache if exists, and make a cache entry if it does not.

**nsob.get** _server key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _server_     | String. The name of the sob server. |
| _key_        | String. The file name or key of the data. |


| **Result** | **Description** |
|:-----------|:----------------|
| `STRING`   | Success. The contents of the file. |
| `NULL`     | Success. The communication was successful, but there was no such key. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nsob.delete ###
Used to remove data from the server. If sendflush is enabled (default), a [ncf](ncf.md) message will be sent to all clients.

**nsob.delete** _server key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _server_     | String. The name of the sob server. |
| _key_        | String. The file name or key of the data. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The file has been deleted. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### nsob.names ###
Used to list the names of the configured sobs.  Both clients and servers are listed.

**nsob.names**

| **Result** | **Description** |
|:-----------|:----------------|
| `String`   | Success. The names of the configured sobs in TCL\_LIST format. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |


```
--------------------------------------------------------------------------------
```

### nsob.debug ###
Used to toggle debug mode. Debug mode adds log messages for many nsob commands.

**nsob.debug** _?boolean?_

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

### nsob.copy ###
Used to copy the contents of a local file to the borker.  If sendflush is enabled (default), a [ncf](ncf.md) message will be sent to all clients.

**nsob.copy** _localFile server key_

| **Argument** | **Description** |
|:-------------|:----------------|
| _localFile_  | String. The complete file path to the file. |
| _server_     | String. The name of the sob server. |
| _key_        | String. The file name or key of the data. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The file was copied to the server. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |


## Configuration ##
The bundled configuration has a single AOLserver instance configured with more than one nsob server.  You could easily break out each nsob server to its own AOLserver instance.  Below is a simple configuration that creates a nsob server called "sob".

#### Server: ####
```
    ns_section "ns/server/$server/modules"
        ns_param nssock nssock.so
        ns_param dci dci.so

    ns_section "ns/server/$server/module/nssock"
        ns_param hostname $hostName
        ns_param address $address
        ns_param port $httpPort

    ns_section "ns/server/$server/module/dci/rpc/server/nsobc:sob"
        ns_param http 1

    ns_section "ns/server/$server/module/dci/rpc/server/nsobc:sob/acl"
        ns_paran allow 127.0.0.1

    ns_section "ns/server/$server/module/dci/sob"
        ns_param debug 1
 
    ns_section "ns/server/$server/module/dci/sob/servers"
        ns_param sob sob

    ns_section "ns/server/$server/module/dci/sob/server/sob"
        ns_param root $sobRoot
        ns_param mkdirs 1
        ns_param rename 1
        ns_param statsdetail 1

    ns_section "ns/server/$server/module/dci/sob/server/sob/clients"
        # This section has no params but is required.
        # Legacy client list before HTTP RPC.
        # See the rpc/server acl section above.
```
#### Client: ####
```
    ns_section "ns/server/$server/modules"
        ns_param dci dci.so

    ns_section "ns/server/$server/module/dci/rpc"
        ns_param debug 1
        ns_param address $address
        ns_param port $rpcPort

    ns_section "ns/server/$server/module/dci/rpc/client/nsobc:sob"
        ns_param address $sobHostAddress
        ns_param port $sobHostHttpPort 
        ns_param httpkeepalive true
        ns_param httpnumconnections 1

    ns_section "ns/server/$server/module/dci/sob"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/sob/clients"
        ns_param sob sob

    ns_section "ns/server/$server/module/dci/sob/client/sob"
        ns_param timeout 1
        ns_param flushOnWrite 1 ; # Default. See Best Practices.
```

## Usage ##
Any client can "put" data on the server. Use the tool server's [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to send some data to the sob server (See [Topology](Topology.md) for more information):

```
    tool:nscp 1> nsob.put sob mySobFile "my sob data"
```

Any client can "get" data from the server. Use the frontend's [Control Port](http://code.google.com/p/aolserver/wiki/nscp) to request data from the sob server (See [Topology](Topology.md) for more information):

```
    frontend:nscp 1> nsob.get sob mySobFile
    my sob data
```

## Caching and Network Cache Flush ##
To optimize performance, nsob has the ability to cache at both the server and client. With caching enabled (default), the initial client request first looks into its own cache. If no entry is found it will make a call to the server. If the server has caching enabled (default), it will first look in its cache. If no entry is found, it will read from disk, make a cache entry, and return the data. The client API will make a cache entry and return the data.

Network Cache Flush ([ncf](ncf.md)) is another service availble in nsdci. With the sobs configured to send cache flush messages (default), the remote client cache will be flushed when a sob entry is addded or changed. Note: You must configure [ncf](ncf.md) on the AOLserver instance hosting the nsob server in order to send cache flush messages.

There are significant performance gains from caching. Consider a fetch to "file.xml" which is 8KB:

```
    tool:nscp 1> time {nsob.get sob file.xml}
    555 microseconds per iteration

    tool:nscp 2> time {nsob.get sob file.xml} 100
    79.08 microseconds per iteration
```

If dubugging is turned on, you will see the following log entries in the tool.server.log file:
```
    ...[-nscp:0-] Notice: sob: sob: get file.xml - ok
    ...[-nscp:0-] Notice: sob: sob: get file.xml - ok (cached)
```

The first entry shows that the data was fetched from the server, the second shows the data was retrieved from cache. Similar notices can be seen on the server in the sob.server.log file:

```
    ...[-conn:0-] Notice: read: file.xml
    ...[-conn:0-] Notice: read: file.xml (cached)
```

## Best Practices ##
Caching should be used where possible to optimize performance. The AOLserver Stats interface has insight to cache hit-rate information and should be used to measure the performance of the nsob cache. A high cache hit-rate is preferred.

You might find that flowing content one way per server gives better performance.  For example, one server brokering data from the tool server to the frontend, and a second from the frontend to the tool server. This could perform better than one nsob server doing both.

When debugging, always check logs on both the client and server. If **nsob.put** fails, there might be insight in the sob server's server log.

When writing from a client that receives ncf messages, you should turn off the flushOnWrite param in the client config. This will cause the client to write to the server without removing its cache entry allowing the ncf message to remove it instead. Useful if the server is set up to send ncf messages.