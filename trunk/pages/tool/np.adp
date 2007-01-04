<%
foreach poll [nps.find] {
    ns_adp_puts "$poll -> [nps.vote $poll]<br>"
}
%>
