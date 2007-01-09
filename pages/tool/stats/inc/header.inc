<%
set title [ns_adp_argv 1]
set javascriptFiles [ns_adp_argv 2]

set debug [ns_queryget debug "false"]
%>

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
    <title><%=$title%></title>
    <link rel="stylesheet" type="text/css" href="/css/global.css">
    <script type="text/javascript">
    djConfig = { 
        isDebug: <%=$debug%>
    };
    </script>
    <script src="/dojo/dojo.js"></script>
    <%
    foreach javascriptFile $javascriptFiles {
        ns_adp_puts "<script src=\"$javascriptFile\"></script>"
    }
    %>
</head>
<body>
