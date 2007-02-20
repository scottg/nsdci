<script type="text/javascript">
    dojo.require("dojo.io.*");
    dojo.addOnLoad(getData);

    function getData() {
        dojo.io.bind ({
            url: '<%=[page.getValue jsonUrl]%>',
            load: function(type, data, evt) {
                displayStatsMessage(data);
                displayTimestamp(data);
                displayStatsTable(data);
                setTimeout('getData()', 1000);
            },
            error: function(type, error) {
                displayStatsError(type, error);
                setTimeout('getData()', 1000);
            },
            mimetype: "text/json"
        });
    }
</script>

<div id='error'></div>
<div id='timestamp'></div>
<div id='message'></div>
<div id='stats'></div>
