# 
# Set up the RPC server.
#
ns_section "ns/server/$server/module/dci/rpc/server/nproxy"
    ns_param http 1

ns_section "ns/server/$server/module/dci/rpc/server/nproxy/acl"
    ns_param allow 127.0.0.1

#
# Configure nproxy.
#
ns_section "ns/server/$server/module/dci/nproxy"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nproxy/servers"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/nproxy/server/$server/clients"
    # This section has no params but is required.
    # Legacy client list before HTTP RPC.
    # See the rpc/server acl section above.
