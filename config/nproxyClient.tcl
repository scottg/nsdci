ns_section "ns/server/$server/module/dci/rpc/client/nproxy:myProxy"
    ns_param address [topology.getValue "httpAddress" "tool"]
    ns_param port [topology.getValue "httpPort" "tool"]
    ns_param httpkeepalive true
    ns_param httpnumconnections 1

ns_section "ns/server/$server/module/dci/nproxy"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nproxy/clients"
    ns_param myProxy myProxy

ns_section "ns/server/$server/module/dci/nproxy/client/myProxy"
    ns_param timeout 1
