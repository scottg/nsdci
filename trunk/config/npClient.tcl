ns_section "ns/server/$server/module/dci/np"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/np/client"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/rpc/client/np"
    ns_param address [topology.getValue "rpcAddress" $server]
    ns_param port [topology.getValue "rpcPort" $server]
