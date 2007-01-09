<%
set clients ""
set clientJson ""

foreach name [rpc.clients] {
    rpc.stats $name stats

    set socks ""
    set fields "name:\"$name\""

    foreach key [array names stats] {
        if {[string match "socks" $key]} {
            set socks $stats($key)  
        } else {
            lappend fields $key:\"$stats($key)\"
        }
    }

    if {[llength $socks]} {
        set sockFields ""
        set sockJson ""

        foreach sock $socks {
            foreach {key value} $sock {
                lappend sockFields $key:\"$value\"
            }

            lappend sockJson "\{ [join $sockFields ", "] \}"
        }

        lappend fields "socks: \[ [join $sockJson ", "] \]"
    }

    lappend clientJson "\{ [join $fields ", "] \}"
}
%>
result = {clients: [ <%=[join $clientJson ", "]%> ] }
