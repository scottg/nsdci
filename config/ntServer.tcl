ns_section "ns/server/$server/module/dci/nt"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nt/server/clients"

foreach ntClient [topology.getValue "ntClients"] {
    set ntAddress [topology.getValue "ntAddress" $ntClient]
    set ntPort [topology.getValue "ntPort" $ntClient]

    ns_param $ntClient $ntAddress:$ntPort
}

ns_section "ns/server/$server/module/dci/nt/server"
    ns_param backupfile /tmp/nt.bak
