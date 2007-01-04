<%
foreach bucket [nt.buckets] {
    ns_adp_puts "$bucket -> [nt.dump $bucket]<br>"
}
%>
