set home [file dirname [ns_info config]]
set pageRoot $home/servers/$server/pages

regsub -all "/etc" $home "" home

ns_section "ns/parameters"
    ns_param home $home
    ns_param logdebug true
    ns_param serverlog $server.log

ns_section "ns/mimetypes"
    ns_param default "*/*"
    ns_param .adp "text/html; charset=iso-8859-1"

ns_section "ns/encodings"
    ns_param adp "iso8859-1"

ns_section "ns/threads"
    ns_param stacksize [expr 128 * 1024]

ns_section "ns/servers"
    ns_param $server "$server"

ns_section "ns/server/$server"
    ns_param directoryfile "index.htm,index.html,index.adp"
    ns_param pageroot $pageRoot
    ns_param maxthreads 20
    ns_param minthreads 5
    ns_param maxconns 20
    ns_param urlcharset "utf-8"
    ns_param outputcharset "utf-8"
    ns_param inputcharset "utf-8"

ns_section "ns/server/$server/adp"
    ns_param map "/*.adp"

ns_section "ns/server/$server/modules"
    ns_param nssock nssock.so
    ns_param nslog nslog.so
    ns_param nscp nscp.so

ns_section "ns/server/$server/module/nssock"
    ns_param hostname [topology.getValue "httpHost" $server]
    ns_param address [topology.getValue "httpAddress" $server]
    ns_param port [topology.getValue "httpPort" $server]

ns_section "ns/server/$server/module/nslog"
    ns_param rolllog true
    ns_param rollonsignal true
    ns_param rollhour 0
    ns_param maxbackup 2
    ns_param file $home/log/$server.access
    
ns_section "ns/server/$server/module/nscp"
    ns_param address [topology.getValue "nscpAddress" $server]
    ns_param port [topology.getValue "nscpPort" $server]
    ns_param cpcmdlogging "false"

ns_section "ns/server/$server/module/nscp/users"
    ns_param user ":"
