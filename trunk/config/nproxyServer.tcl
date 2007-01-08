ns_section "ns/server/$server/module/dci/rpc/server/nproxy"
    ns_param http 1

ns_section "ns/server/$server/module/dci/nproxy"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nproxy/servers"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/nproxy/server/$server"
    ns_param handshakeName myProxy

ns_section "ns/server/$server/module/dci/nproxy/server/$server/clients"
    # This section has no params but is required
