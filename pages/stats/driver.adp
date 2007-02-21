<%
    page.setValue title "Driver"
    page.setValue debug [ns_queryget debug "false"]
    page.setValue url "json/driver.json"
  
    ns_adp_include inc/start.inc 
    ns_adp_include inc/header.inc
    ns_adp_include inc/driver.inc
    ns_adp_include inc/end.inc
%>
