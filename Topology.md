# Topology #

The bundled setup comes with three servers.

## Frontend ##

The "frontend" server is considered a client, and has connections to both the "tool" and "SOB" servers. It is generally where forward-facing Web application are hosted.

| **Service** | **Address** | **Port** |
|:------------|:------------|:---------|
| http        | 127.0.0.1   | 9000     |
| nscp        | 127.0.0.1   | 9900     |
| rpc         | 127.0.0.1   | 9800     |
| receiver    | 127.0.0.1   | 9700     |
| eval        | 127.0.0.1   | 9600     |
| nt          | 127.0.0.1   | 9500     |

## Tool ##

The "tool" server is where all the services are hosted: networked poll (np), networked tally (nt), networked ratings (nrate), networked variables (nv), etc. You could just as easily create separate servers for each service, but for simplicity, a single tool server is provided.

| **Service** | **Address** | **Port** |
|:------------|:------------|:---------|
| http, rpc   | 127.0.0.1   | 8000     |
| rpc         | 127.0.0.1   | 8800     |
| nscp        | 127.0.0.1   | 8900     |
| receiver    | 127.0.0.1   | 8700     |

## SOB ##

The "SOB" server stores and publishes content from both the "frontend" and "tool" servers.

| **Service** | **Address** | **Port** |
|:------------|:------------|:---------|
| http, rpc   | 127.0.0.1   | 7000     |
| nscp        | 127.0.0.1   | 7900     |

## topology.tcl ##

At start up, the "topology.tcl" file, found in the etc/ directory, creates an array of client lists and ports for each service as well as IP addresses for each server. This array is used by the server specific configuration files to make the needed connections and listen on the correct ports. See "[Running](Running.md)" on the Wiki for information on starting servers.