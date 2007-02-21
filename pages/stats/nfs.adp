<% 
    page.setValue title "NFS Servers"
    page.setValue debug [ns_queryget debug "false"]
    page.setValue url "json/nfs.json"
  
    ns_adp_include inc/start.inc 
    ns_adp_include inc/header.inc
    ns_adp_include inc/nfs.inc
    ns_adp_include inc/end.inc
%>
