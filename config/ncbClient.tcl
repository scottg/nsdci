ns_section "ns/server/$server/module/dci/rpc/client/nsobc:ncb0"
    ns_param address [topology.getValue "httpAddress" "sob"]
    ns_param port [topology.getValue "httpPort" "sob"]
    ns_param httpkeepalive true
    ns_param httpnumconnections 1

ns_section "ns/server/$server/module/dci/sob/clients"
    ns_param ncb0 ncb0
