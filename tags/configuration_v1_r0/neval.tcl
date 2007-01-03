ns_section "ns/server/$server/module/dci/neval"
    ns_param address [topology.getValue "nevalAddress" $server]
    ns_param port [topology.getValue "nevalPort" $server]
    ns_param password [topology.getValue "nevalPassword" $server]
    ns_param allowed 127.0.0.1

