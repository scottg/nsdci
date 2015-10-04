# Networked Poll (nps, nps2, npc, npc2) #
nsdci/dcicommon/poll.c

## Introduction ##
The Networked Poll api (npc, npc2, nps, nps2) is is used to maintain votes for specific positions in a bucket of 20 choices. Similar to Networked Tally api ([nt](nt.md)), Networked Poll keeps a running total, but not for user-defined keys. And unlike Networked Tally, the bucket does not need to be "swept".  The api is only exposed when properly configured. See the "Configuration" section for more information.

Additional Reading: [rpc](rpc.md)

## TCL API ##
```
--------------------------------------------------------------------------------
```
npc and npc2 commands are exposed on the client, while nps and nps2 commands are exposed on the server.
### npc.vote, nps.vote ###
Used to send a choice for a named poll to the poll server and return the current standing. A choice > 20 or < 0 will return the current standing without adding a vote.  See the "Usage" section for examples.  When using _nps.vote_ the _choice_ argument is optional. If no _choice_ argument is given the current standings are returned.

**npc.vote** _poll choice_

**nps.vote** _poll ?choice?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _poll_       | String. The name of the poll. Polls are self initializing. |
| _choice_     | Integer. The choice selected one through 20. |

| **Result** | **Description** |
|:-----------|:----------------|
| `TCL_LIST` | Success. The choice was tallied and the current standings returned. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### npc2.vote, nps2.vote ###
Used to adjust the poll standing. This is an admin utility and should not be used cast votes.  The choice is adjusted as is the total votes of the poll. See the "Usage" section for examples. When using _nps2.vote_ the _choice_ argument and the _adjust_ argument are optional. If no _choice_ and _adjust_ arguments are given the current standing is  returned.

**npc2.vote** _poll choice adjust_

**nps2.vote** _poll ?choice? ?adjust?_

| **Argument** | **Description** |
|:-------------|:----------------|
| _poll_       | String. The name of the poll. Polls are self initializing. |
| _choice_     | Integer. The choice selected one through 20. |
| _adjust_     | Integer. The amount + or - to adjust. |


| **Result** | **Description** |
|:-----------|:----------------|
| `TCL_LIST` | Success. The choice was adjusted, the total was adjusted, and the adjusted standings retunred. |
| `TCL_ERROR` | Failure. An error occurred. See the server log for more information. |

```
--------------------------------------------------------------------------------
```

### npc.debug, nps.debug ###
Used to retrieve the current debug value, or to set a new debug value.

**npc.debug** _?boolean?_

**nps.debug** _?boolean?_

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

### nps.database ###

### nps.find ###

### nps.backup ###

### nps.delete ###

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

    ns_section "ns/server/$server/module/dci/rpc/server/np"
        ns_param http 1

    ns_section "ns/server/$server/module/dci/np"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/np/server/clients"
        # This section has no params but is required.
        # Legacy client list before HTTP RPC.
        # See the rpc/server acl section above. 
```
#### Client: ####
```
    ns_section "ns/server/$server/module/dci/np"
        ns_param debug 1

    ns_section "ns/server/$server/module/dci/np/client"
        ns_param $server $server

    ns_section "ns/server/$server/module/dci/rpc/client/np"
        ns_param address $npServerAddress
        ns_param port $npServerHttpPort
        ns_param httpkeepalive true
        ns_param httpnumconnections 1

    ns_section "ns/server/$server/module/dci/rpc/client/np2"
        ns_param address $npServerAddress
        ns_param port $npServerHttpPort
        ns_param httpkeepalive true
        ns_param httpnumconnections 1
```

## Usage ##
When using _npc.vote_ you are selecting one choice out of 20.  The index is 0 based so you select 0 through 19.  Each vote adds to the total votes which is in the first position of the returned list.  Using the frontend [Control Port](http://code.google.com/p/aolserver/wiki/nscp) send some votes to the poll server (see [Topology](Topology.md) for more detail):
```
    % telnet 127.0.0.1 9900
    Trying 127.0.0.1...
    Connected to localhost.
    Escape character is '^]'.
    login: 
    Password: 

    Welcome to frontend running at /usr/local/aolserver/bin/nsd (pid 6928)
    AOLserver/4.5.0 (aolserver4_5) for osx built on Jan  4 2007 at 17:11:28
    CVS Tag: $Name:  $
    frontend:nscp 1> npc.vote myPoll 3
    1 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    frontend:nscp 2> npc.vote myPoll 13
    2 0 0 0 1 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 3> npc.vote myPoll 0
    3 1 0 0 1 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
```

The first vote was for choice 4 (remember the index is 0 based). The result of that command was the current standings with the total number of votes across the poll in the first position.  The second vote was for the choice 14. The third for choice 1. Each time the current standing was returned and the total number of votes increased. Add more votes:

```
    frontend:nscp 4> npc.vote myPoll 3
    4 1 0 0 2 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 5>  npc.vote myPoll 3
    5 1 0 0 3 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 6>  npc.vote myPoll 3
    6 1 0 0 4 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 7>  npc.vote myPoll 3
    7 1 0 0 5 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 8>  npc.vote myPoll 3
    8 1 0 0 6 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
    frontend:nscp 9>  npc.vote myPoll 3
    9 1 0 0 7 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
```

At this point we have 9 total votes and choice 4 is leading the pack with 7 out 9 votes.  You can use the npc2 admin api to adjust or reset the poll. See "Best Practices" for more detail:

```
    frontend:nscp 10> npc2.vote myPoll 3 -2
    7 1 0 0 5 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0
```

The total votes for choice 4 has been decreased by 2, as has the total number of votes on the poll.


## Best Practices ##
For security, you should not configure the np clients with the npc2 api. This will avoid giving adjustment access to voting clients.

You can dynamically create polls in adp pages by creating a poll.adp page which would request poll data with the poll name and questions, based on url query data, from another resource and use the npc api to cast votes and reconcile the results with the questions.