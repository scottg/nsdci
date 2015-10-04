# Networked Tally (nt) #
nsdci/dcicommon/otally.c

## Introduction ##
Networked Tally provides a simple mechanism for collecting counts from remote servers. Clients send counts to the tally server.  The tally server stores them in user-defined keys, which are grouped into buckets. The tally server then processes or "sweeps" each bucket using a callback.

Clients have a configurable buffer which allows tallies to be collected even if the server is not available. The server stores data on disk for persistent storage across restarts. It's important to note, however, that the backup file is only written at server exit in the case of a proper shutdown, or if additional code is written to explicitly backup the data. The api is only exposed when properly configured. See the "Configuration" section for more information.

## Tcl API ##
```
--------------------------------------------------------------------------------
```
### nt.debug ###
Used to retrieve the current debug value, or to set a new debug value.

**nt.debug** _?boolean?_

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

### nt.send ###
Used to add _value_ to the _key_ in _bucket_.

**nt.send** _bucket_ _key_ _value_

| **Argument** | **Description** |
|:-------------|:----------------|
| _bucket_     | String. Name of the tally bucket. Buckets are self initializing. |
| _key_        | String. Name of the key to set in the specified bucket. |
| _value_      | Integer. The value to add to the specified key, in the specified bucket. |


| **Result** | **Description** |
|:-----------|:----------------|
| `NULL`     | Success. The result was successfully written to the nt server. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

## nt.dump ##
Used to dump the contents of a bucket. This does not reset the bucket. The results is a tcl list of tcl lists. Each list has key name, number of votes, and the total count.  Since each vote can add any numeric value (see _nt.send_ ), the number of votes do not have to equal total count.

**nt.dump** _bucket_

| **Argument** | **Description** |
|:-------------|:----------------|
| _bucket_     | String. Name of the tally bucket. Buckets are self initializing. |


| **Result** | **Description** |
|:-----------|:----------------|
| `String`   | Success. Returns a tcl list of lists. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

## nt.exists ##
Checks to see _bucket_ exists.

**nt.exists** _bucket_

| **Argument** | **Description** |
|:-------------|:----------------|
| _bucket_     | String. Name of the tally bucket. Buckets are self initializing. |


| **Result** | **Description** |
|:-----------|:----------------|
| `boolean`  | Success. 1 the bucket exists. 0 the bucket does not exist. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

nt.get nt.read nt.buckets nt.debug nt.write nt.peek nt.send

## Configuration ##

#### Server: ####
```
    ns_section ns/server/$server/module/dci/nt
        ns_param debug 1
 
    ns_section ns/server/$server/module/dci/nt/server
        ns_param backupfile ntData.bak
 
    ns_section ns/server/$server/module/dci/nt/server/clients
        ns_param frontend $rpcAddress:$rpcPort
```

#### Client: ####
```
    ns_section ns/server/$server/module/dci/nt
        ns_param debug 1

    ns_section ns/server/$server/module/dci/nt/client
        ns_param address $rpcAddress
        ns_param port $rpcPort
        ns_param max 1000
```

## Usage ##
Each bucket is "swept" by the server using a bucket-specific callback. The following example adds the current key, count, totals of a bucket to a [nv](nv.md):

**tallyCallback** _key_ _count_ _total_

| **Argument** | **Description** |
|:-------------|:----------------|
| _key_        | String. Name of the key. |
| _count_      | Integer. Current count for the specified key. |
| _total_      | Integer. Total count for all keys. |

```
    proc tallyCallback {key count total} {
        if {[nv.cget nv ${key}.total oldTotal]} {
            incr total $oldTotal
        }
   
        if {[nv.cget nv ${key}.count oldCount]} {
            incr count $oldCount
        }

        nv.set nv ${key}.total $total
        nv.set nv ${key}.count $count
    }   
```

Using the frontend AOLserver Control Port, send some votes to the tally server (See [Topology](Topology.md) for more information):

```
    % telnet 127.0.0.1 9900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password: 
 
    Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 6288)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    frontend:nscp 1> nt.send myBucket foo 5
 
    frontend:nscp 2> nt.send myBucket bar 1

    frontend:nscp 3> exit

    Goodbye!
    Connection closed by foreign host.
```

Using the tool AOLserver Control Port, dump the bucket to see what will be processed (See [Topology](Topology.md) for more information):
```
    [neon:~] Michael% telnet 127.0.0.1 8900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password: 

    Welcome to tool running at /usr/local/aolserver/bin/nsd (pid 6287)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    tool:nscp 1> nt.dump myBucket
    {bar 1 1} {foo 1 5}
```

The result is a tcl list of tcl lists. Each list has key name, number of votes, and the total count. Since each vote can add any numeric value (see nt.send ), the number of votes do not have to equal total count. Now process the bucket by using _nt.get_ and the callback:

```
    tool:nscp 2> nt.get myBucket tallyCallback
    1
```

The [nv](nv.md) has been updated and the bucket reset:

```
    tool:nscp 3> nv.dump nv
    bar.count 1 bar.total 1 foo.total 5 foo.count 1
 
    tool:nscp 4> nt.dump myBucket

    tool:nscp 5> exit

    Goodbye!
    Connection closed by foreign host.
    [neon:~] Michael% 
```
You can use the ns schedule API to schedule the sweep as needed.