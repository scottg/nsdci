ns_section "ns/server/$server/module/dci/np"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/np/server/clients"

foreach npClient [topology.getValue "npClients"] {
    set rpcAddress [topology.getValue "rpcAddress" $npClient]
    set rpcPort [topology.getValue "rpcPort" $npClient]

    ns_param $npClient $rpcAddress:$rpcPort
}

ns_section "ns/server/$server/module/dci/rpc/server/np"
    ns_param http 1
