ns_section "ns/server/$server/module/dci/nv"
    #ns_param address [topology.getValue "nevalAddress" $server]
    #ns_param port [topology.getValue "nevalPort" $server]
    #ns_param password [topology.getValue "nevalPassword" $server]
    #ns_param allowed 127.0.0.1
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nv/arrays"
    ns_param nv nv

ns_section "ns/server/$server/module/dci/nv/array/nv"
    ns_param persist 1
