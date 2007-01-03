ns_section "ns/server/$server/modules"
    ns_param dci dci.so

ns_section "ns/server/$server/module/dci/rpc"
    ns_param debug 1
    ns_param address [topology.getValue "rpcAddress" $server]
    ns_param port [topology.getValue "rpcPort" $server]

ns_section "ns/server/$server/module/dci/rpc/client/nsobc:sob"
    ns_param httpkeepalive 1
    ns_param host [topology.getValue "sobAddress" "sob"]
    ns_param httpnumconnections 2

ns_section "ns/server/$server/module/dci/ncf"
    ns_param debug 1
    ns_param client 1

ns_section "ns/server/$server/module/dci/receiver"
    ns_param address [topology.getValue "receiverAddress" $server]
    ns_param port [topology.getValue "receiverPort" $server]

ns_section "ns/server/$server/module/dci/sob"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/sob/clients"
    ns_param sob sob

ns_section "ns/server/$server/module/dci/sob/client/sob"
    ns_param timeout 1