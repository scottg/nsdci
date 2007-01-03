global Topology

set Topology(servers)       [list "frontend" "sob" "tool"]

set Topology(sobClients)    [list "frontend" "tool"]
set Topology(nvClients)     [list "frontend"]
set Topology(ntClients)     [list "frontend"]

set Topology(frontend.httpAddress)      "127.0.0.1"
set Topology(frontend.httpPort)         9000
set Topology(frontend.httpHost)         "localhost"
set Topology(frontend.nscpAddress)      "127.0.0.1"
set Topology(frontend.nscpPort)         9900
set Topology(frontend.rpcAddress)       "127.0.0.1"
set Topology(frontend.rpcPort)          9800
set Topology(frontend.receiverAddress)  "127.0.0.1"
set Topology(frontend.receiverPort)     9700
set Topology(frontend.nevalAddress)     "127.0.0.1"
set Topology(frontend.nevalPort)        9600

#
# nevalPassword = neval (ns_crypt neval t2)
#

set Topology(frontend.nevalPassword)    "t2abr7Y/HAlzU"
set Topology(frontend.ntAddress)        "127.0.0.1"
set Topology(frontend.ntPort)           9500

set Topology(tool.httpAddress)          "127.0.0.1"
set Topology(tool.httpPort)             8000
set Topology(tool.httpHost)             "localhost"
set Topology(tool.nscpAddress)          "127.0.0.1"
set Topology(tool.nscpPort)             8900
set Topology(tool.rpcAddress)           "127.0.0.1"
set Topology(tool.rpcPort)              8800
set Topology(tool.receiverAddress)      "127.0.0.1"
set Topology(tool.receiverPort)         8700

set Topology(sob.httpAddress)           "127.0.0.1"
set Topology(sob.httpPort)              7000
set Topology(sob.httpHost)              "localhost"
set Topology(sob.nscpAddress)           "127.0.0.1"
set Topology(sob.nscpPort)              7900
set Topology(sob.sobAddress)            "127.0.0.1"

proc topology.getServers {} {
    return [topology.getValue "servers"]
}

proc topology.getKeys {server} {
    global Topology
    
    set keys ""
    
    foreach key [array names Topology "$server.*"] {
        lappend keys [lindex [split $key "."] 1]
    }
    
    return $keys
}

proc topology.getValue {key {server ""}} {
    global Topology
    
    if {[string length $server]} {
        set key "$server.$key"
    }
    
    set value ""
    
    if {![info exists Topology($key)]} {
        return ""
    }
    
    return $Topology($key)
}

rename ns_section ns_section.orig
rename ns_param ns_param.orig

proc ns_section {args} {
    puts stdout "CONFIG: ns_section $args"
    
    return [ns_section.orig $args]
}

proc ns_param {args} {
    puts stdout "CONFIG:     ns_param $args"
    
    return [eval [concat ns_param.orig $args]]
}
