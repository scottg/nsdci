ns_section "ns/server/$server/module/dci/nrate"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nrate/client"
    ns_param address [topology.getValue "rpcAddress" $server]
    ns_param port [topology.getValue "rpcPort" $server]

ns_section "ns/server/$server/module/dci/rpc/client/nrate"
    ns_param address [topology.getValue "rpcAddress" $server]
    ns_param port [topology.getValue "rpcPort" $server]
