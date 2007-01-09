<%
set title [ns_adp_argv 1]
set javascriptFiles [ns_adp_argv 2]
set checkAuth [ns_adp_argv 3]
%>

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
    <title><%=$title%></title>
    <link rel="stylesheet" type="text/css" href="/yui/build/container/assets/container.css" />
    <script src="/yui/build/yahoo/yahoo.js"></script>
    <script src="/yui/build/dom/dom.js"></script>
    <script src="/yui/build/event/event.js"></script>
    <script src="/yui/build/connection/connection.js"></script>
    <script src="/yui/build/animation/animation.js"></script>
    <script src="/yui/build/autocomplete/autocomplete.js"></script>
    <script src="/yui/build/container/container.js"></script>  
    <%
    foreach javascriptFile $javascriptFiles {
        ns_adp_puts "<script src=\"$javascriptFile\"></script>"
    }
    %>
</head>
<body>
