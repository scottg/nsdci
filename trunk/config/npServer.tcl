#
# Set up the RPC server.
#
ns_section "ns/server/$server/module/dci/rpc/server/np"
    ns_param http 1

ns_section "ns/server/$server/module/dci/rpc/server/np/acl"
    ns_param allow 127.0.0.1

#
# Configure Networked Poll.
#
ns_section "ns/server/$server/module/dci/np"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/np/server/clients"
    # This section has no params but is required.
    # Legacy client list before HTTP RPC.
    # See the rpc/server acl section above.
