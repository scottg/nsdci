<%
set poll [ns_adp_argv 1]
%>

<b><%=$poll%>:</b><br><br>

<form method=post action=np.adp>
<input type=hidden name=a value=save>
<input type=hidden name=p value="<%=$poll%>">

<s_t>
    <s_h>
        <s_l>Bucket</s_l>
        <s_l>Total</s_l>
     </s_h>
<%
set d [nps.vote $poll]
set t [lindex $d 0]
set bs [lrange $d 1 end]

set i 0
set c 0

foreach b $bs {
    incr c

    if {$i == 0} {
        set s "class=even"
        set i 1
    } else {
        set s "class=odd"
        set i 0
    }

    ns_adp_puts "<s_r>
        <s_c $s>$c</s_c>
        <s_c $s><input type=text name=b_$c value=\"$b\" size=5></s_c>
    </s_r>"
}
%>
</s_t>
<br>
<input type=submit name=save value=Save>
</form>
