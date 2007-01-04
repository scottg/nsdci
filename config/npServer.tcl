ns_section "ns/server/$server/module/dci/np"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/np/server/clients"

foreach npClient [topology.getValue "npClients"] {
    set npAddress [topology.getValue "npAddress" $npClient]
    set npPort [topology.getValue "npPort" $npClient]

    ns_param $npClient $npAddress:$npPort
}

ns_section "ns/server/$server/module/dci/np/server"
    ns_param database np

ns_section "ns/server/$server/module/dci/rpc/server/np"
    ns_param http 1
