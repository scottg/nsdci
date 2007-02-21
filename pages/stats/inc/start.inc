<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
    <title><%=[page.getValue title]%></title>
    <link rel="stylesheet" type="text/css" href="css/stats.css">
    <script type="text/javascript">
        djConfig = {
            isDebug: <%=[page.getValue debug]%>
        };
    </script>
    <script src="dojo/dojo.js"></script>
    <script type="text/javascript">
        dojo.require("dojo.widget.FilteringTable");
        dojo.hostenv.writeIncludes();
    </script>
    <script src="js/stats.js"></script>
</head>
<body>
