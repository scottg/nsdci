<%
    lappend nav {RPC Clients} rpc.adp {Cache} cache.adp
    lappend nav {NFS Servers} nfs.adp {Driver} driver.adp
    lappend nav {Threads} threads.adp

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

<div id=pause>
<a href="javascript:controlStats('pause');">Pause</a>
</div>
<div id=resume style="display:none">
<a href="javascript:controlStats('resume');">Resume</a>
</div>

<script>
    var UpdateStats = 1;
</script>
