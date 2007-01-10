<%
set clients ""
set clientJson ""
set clientHdrs ""
set socksHdrs ""

set clientHdr(nsockerror) "nsockerror"
set clientHdr(nsend) "nsend"
set clientHdr(tmin) "tmin"
set clientHdr(nsocks) "nsocks"
set clientHdr(tavg) "tavg"
set clientHdr(tmax) "tmax"
set clientHdr(socks) "socks"
set clientHdr(nsockwait) "nsockwait"
set clientHdr(nsendwait) "nsendwait"
set clientHdr(nnosocks) "nnosocks"
set clientHdr(nrecv) "nrecv"
set clientHdr(nrecvwait) "nrecvwait"
set clientHdr(nsocktimeout) "nsocktimeout"
set clientHdr(nreqtimeout) "nreqtimeout"
set clientHdr(nrequest) "nrequest"

set socksHdr(connid) "connid"
set socksHdr(peeraddr) "peeraddr"
set socksHdr(peerport) "peerport"
set socksHdr(atime) "atime"
set socksHdr(mtime) "mtime"
set socksHdr(nrequest) "nrequest"
set socksHdr(nrecv) "nrecv"
set socksHdr(nsend) "nsend"

foreach key [array names clientHdr] {
    lappend clientHdrs $key:\"$clientHdr($key)\"
}

foreach key [array names socksHdrs] {
    lappend socksHdrs $key:\"$socksHdr($key)\"
}

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
result = {
    timestamp:"<%=[ns_time]%>", 
    headers: { 
        clients: { 
            <%=[join $clientHdrs ", "]%> 
        }, 
        socks: { 
            <%=[join $socksHdrs ", "]%> 
        } 
    },
    clients: [
        <%=[join $clientJson ", "]%> 
    ]
}
