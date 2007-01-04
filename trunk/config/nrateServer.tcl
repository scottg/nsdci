ns_section "ns/server/$server/module/dci/nrate"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nrate/server/clients"

foreach nrateClient [topology.getValue "nrateClients"] {
    set rpcAddress [topology.getValue "rpcAddress" $nrateClient]
    set rpcPort [topology.getValue "rpcPort" $nrateClient]

    ns_param $nrateClient $rpcAddress:$rpcPort
}

ns_section "ns/server/$server/module/dci/rpc/server/nrate"
    ns_param http 1
