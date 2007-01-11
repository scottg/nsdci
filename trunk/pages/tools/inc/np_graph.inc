<%
set poll [ns_adp_argv 1]

set d [nps.vote $poll]
set t [lindex $d 0]
set bs [lrange $d 1 end]

set maxWidth 500
set i 0

ns_adp_puts "<b>$poll</b><br><br><s_t style='width:600px'>"

foreach b $bs {
    incr i

    if {$t == 0} {
        set w 0
    } else {
        set p [expr $b.0 / $t.0]
        set w [format %.0f [expr $maxWidth * $p]]
    }

    set per [format %.0f [expr $p * 100.0]]
    set text "${per}% ($b)"

    if {$per < 10} {
        set text "&nbsp;"
    }

    if {$b == 0} {
        set text ""
    }

    ns_adp_puts "<s_r>
        <s_c>$i:</s_c>
        <s_c style='background-color:#ececec'>
            <s_t>
            <s_r>
                <s_c style='width:${w}px; background-color:#666'>
                <span style='font-size:7pt;color:#ffffff'>$text</span>
                </s_c>
            </s_r>
            </s_t>
        </s_c>
    </s_r>"
}
%>

</s_t>
