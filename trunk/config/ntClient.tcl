ns_section "ns/server/$server/module/dci/nt"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nt/client"
    ns_param address [topology.getValue "ntAddress" $server]
    ns_param port [topology.getValue "ntPort" $server]
