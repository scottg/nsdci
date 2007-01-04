ns_section "ns/server/$server/module/dci/sob/servers"
    ns_param ncb0 ncb0

ns_section "ns/server/$server/module/dci/sob/server/ncb0"
    ns_param root "/tmp/ncb0"
    ns_param mkdirs 1
    ns_param rename 1
    ns_param statsdetail 1
    ns_param nocache 0
    ns_param sendflush 0

ns_section "ns/server/$server/module/dci/sob/server/ncb0/clients"

foreach ncbClient [topology.getValue "ncbClients"] {
    set rpcAddress [topology.getValue "rpcAddress" $ncbClient]
    set rpcPort [topology.getValue "rpcPort" $ncbClient]

    ns_param $ncbClient $rpcAddress:$rpcPort
}
