ns_section "ns/server/$server/modules"
    ns_param dci dci.so
    
ns_section "ns/server/$server/module/dci/server"
    ns_param debug 1
    ns_param retryinterval 1

ns_section "ns/server/$server/module/dci/rpc/server/nsobc:sob"
    ns_param http 1

ns_section "ns/server/$server/module/dci/rpc/server/nsobc:sob/allowed"
    ns_param frontend "127.0.0.1"
    ns_param tool "127.0.0.1"

ns_section "ns/server/$server/module/dci/broadcast"
    ns_param debug 1
    ns_param timeoutinterval 2

ns_section "ns/server/$server/module/dci/ncf"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/ncf/clients"

foreach sobClient [topology.getValue "sobClients"] {
    set receiverAddress [topology.getValue "receiverAddress" $sobClient]
    set receiverPort [topology.getValue "receiverPort" $sobClient]
    
    ns_param $sobClient $receiverAddress:$receiverPort
}

ns_section "ns/server/$server/module/dci/nfs"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/sob"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/sob/servers"
    ns_param sob sob

ns_section "ns/server/$server/module/dci/sob/server/sob"
    ns_param root "/tmp/sob"
    ns_param mkdirs 1
    ns_param rename 1
    ns_param statsdetail 1

ns_section "ns/server/$server/module/dci/sob/server/sob/clients"
