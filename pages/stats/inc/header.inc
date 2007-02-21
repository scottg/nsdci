<%
    lappend nav {RPC Clients} rpc.adp {Cache} cache.adp
    lappend nav {NFS Servers} nfs.adp

    set thisPage [lindex [ns_conn urlv] end]

    foreach {display page} $nav {
        if {[string equal $page $thisPage]} {
            ns_adp_puts "\[${display}\]"
            continue
        }

        ns_adp_puts "\[<a href=\"${page}\">${display}</a>\]"
    }

    ns_adp_puts "<h1>[page.getValue title]</h1>" 
%>
