<%
    page.setValue title "Threads"
    page.setValue debug [ns_queryget debug "false"]
    page.setValue url "json/threads.json"
  
    ns_adp_include inc/start.inc 
    ns_adp_include inc/header.inc
    ns_adp_include inc/threads.inc
    ns_adp_include inc/end.inc
%>
