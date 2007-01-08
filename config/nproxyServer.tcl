ns_section "ns/server/$server/module/dci/rpc/server/nproxy"
    ns_param http 1

ns_section "ns/server/$server/module/dci/nproxy"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nproxy/servers"
    ns_param nproxy nproxy

ns_section "ns/server/$server/module/dci/nproxy/server/nproxy"
    ns_param handshakeName myProxy

ns_section "ns/server/$server/module/dci/nproxy/server/nproxy/clients"
    # This section has no params but is required
