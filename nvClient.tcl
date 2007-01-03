ns_section "ns/server/$server/module/dci/nv"
    ns_param debug 1

ns_section "ns/server/$server/module/dci/nv/arrays"
    ns_param nv nv

ns_section "ns/server/$server/module/dci/nv/array/nv"
    ns_param persist 1

ns_section "ns/server/$server/module/dci/nv/clients"
    ns_param $server $server

ns_section "ns/server/$server/module/dci/nv/client/$server"
    ns_param login "nv"

ns_section "ns/server/$server/module/dci/nv/client/$server/arrays"
    ns_param nv nv
