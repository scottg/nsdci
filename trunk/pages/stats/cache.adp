<%
    page.setValue title "Cache"
    page.setValue debug [ns_queryget debug false]
    page.setValue jsonUrl /stats/json/cache.json
  
    ns_adp_include inc/start.inc 
    ns_adp_include inc/header.inc
    ns_adp_include inc/stats.inc
    ns_adp_include inc/end.inc
%>
