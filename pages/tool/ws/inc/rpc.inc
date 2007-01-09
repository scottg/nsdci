<%
package require tdom

set doc [dom createDocument "response"]
set root [$doc documentElement]

$root setAttribute "status" "OK"

dom createNodeCmd textNode "text"
dom createNodeCmd elementNode "clients"
dom createNodeCmd elementNode "client"
dom createNodeCmd elementNode "name"
dom createNodeCmd elementNode "socks"
dom createNodeCmd elementNode "sock"

$root appendFromScript {
    "clients" {
        foreach name [rpc.clients] {
            client {
                name {
                    text $name
                }

                rpc.stats $name stats
                set socks ""

                foreach key [array names stats] {
                    dom createNodeCmd elementNode $key

                    if {![string match "socks" $key]} { 
                        ${key} {
                            text $stats($key)
                        }
                    } else {
                        set socks $stats($key)
                    }
                }

                if {[llength $socks]} {
                    socks {
                        foreach sock $socks {
                            sock {
                                foreach {key value} $sock {
                                    dom createNodeCmd elementNode $key

                                    ${key} {
                                        text $value
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

ns_adp_puts "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
ns_adp_puts [$root asXML]
%>
