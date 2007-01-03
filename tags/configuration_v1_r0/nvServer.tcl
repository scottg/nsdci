ns_section "ns/server/$server/module/dci/nv"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nv/arrays"
    ns_param nv nv

ns_section "ns/server/$server/module/dci/nv/array/nv"
    ns_param persist 1

ns_section "ns/server/$server/module/dci/nv/servers"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/nv/server/$server"
    ns_param login "nv"

ns_section "ns/server/$server/module/dci/nv/server/$server/clients"

foreach nvClient [topology.getValue "nvClients"] {
    set receiverAddress [topology.getValue "receiverAddress" $nvClient]
    set receiverPort [topology.getValue "receiverPort" $nvClient]

    ns_param $nvClient $receiverAddress:$receiverPort
}

ns_section "ns/server/$server/module/dci/nv/server/$server/arrays"
    ns_param nv nv
