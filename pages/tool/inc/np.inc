<s_t>
    <s_h>
        <s_l></s_l>
        <s_l></s_l>
        <s_l></s_l>
        <s_l>Name</s_l>
        <s_l>Total</s_l>
        <s_l>1</s_l>
        <s_l>2</s_l>
        <s_l>3</s_l>
        <s_l>4</s_l>
        <s_l>5</s_l>
        <s_l>6</s_l>
        <s_l>7</s_l>
        <s_l>8</s_l>
        <s_l>9</s_l>
        <s_l>10</s_l>
        <s_l>11</s_l>
        <s_l>12</s_l>
        <s_l>13</s_l>
        <s_l>14</s_l>
        <s_l>15</s_l>
        <s_l>16</s_l>
        <s_l>17</s_l>
        <s_l>18</s_l>
        <s_l>19</s_l>
        <s_l>20</s_l>
     </s_h>
<%
set i 0

foreach n [nps.find] {
    set d [nps.vote $n]
    set t [lindex $d 0]
    set bs [lrange $d 1 end]

    if {$i == 0} {
        set s "class=even"
        set ts "class=teven"
        set i 1
    } else {
        set s "class=odd"
        set ts "class=todd"
        set i 0
    }

    ns_adp_puts "<s_r>
        <s_c $s><a href=np.adp?a=edit&p=$n><img src=img/wand.gif border=0></a></s_c>
        <s_c $s><a href=np.adp?a=delete&p=$n><img src=img/trash.gif border=0></a></s_c>
        <s_c $s><a href=np.adp?a=graph&p=$n><img src=img/graph.gif border=0></a></s_c>
        <s_c $s>$n</s_c>
        <s_c $ts>$t</s_c>"

    foreach b $bs {
        ns_adp_puts "<s_c $s>$b</s_c>"
    }
  
    ns_adp_puts "</s_r>"
}
%>
</s_t>
