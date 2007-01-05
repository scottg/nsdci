ns_section "ns/server/$server/module/dci/np"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/np/client"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/rpc/client/np"
    ns_param address [topology.getValue "httpAddress" "tool"]
    ns_param port [topology.getValue "httpPort" "tool"]
    ns_param httpkeepalive true
    ns_param httpnumconnections 1

ns_section "ns/server/$server/module/dci/rpc/client/np2"
    ns_param address [topology.getValue "httpAddress" "tool"]
    ns_param port [topology.getValue "httpPort" "tool"]
    ns_param httpkeepalive true
    ns_param httpnumconnections 1
