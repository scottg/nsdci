<script type="text/javascript">
    dojo.require("dojo.io.*");
    dojo.addOnLoad(getData);
  
    function getData() {
        dojo.io.bind ({
            url: '<%=[page.getValue url ]%>',
            load: function(type, data, evt) {
                updateTimestamp(data.timestamp);
                setText('error', '');
                
                var statsData = [];
                
                for (row in data.rows) {
                    var socks = data.rows[row]['socks'];
                    var sockStr = "";
 
                    for (sock in socks) {
                        sockStr = sockStr + socks[sock]['id'] + ' (' + socks[sock]['state'] + ') <br>';
                    }
                     
                    var o = {
                        Id:row,
                        Name:data.rows[row]['name'],
                        Time:parseInt(data.rows[row]['time']), 
                        Spins:parseInt(data.rows[row]['spins']), 
                        Accepts:parseInt(data.rows[row]['accepts']), 
                        Queued:parseInt(data.rows[row]['queued']), 
                        Reads:parseInt(data.rows[row]['reads']), 
                        Dropped:parseInt(data.rows[row]['dropped']), 
                        Overflow:parseInt(data.rows[row]['overflow']), 
                        Timeout:parseInt(data.rows[row]['timeout']),
                        Socks:sockStr
                    };
                    
                    statsData.push(o);
                }
                
                var w = dojo.widget.byId("statsTable");
                
                w.store.clearData();
    		    w.store.setData(statsData);
                
                updateStats();
            },
            error: function(type, error) {
                updateError(type, error);
                updateStats();
            },
            mimetype: "text/json"
        });
    }
</script>

<div id='error'></div>
<div id='timestamp'></div>
<div id='stats'>
    <table dojoType="filteringTable" id="statsTable" 
        multiple="false" alternateRows="true"
        cellpadding="0" cellspacing="0" border="0" style="margin-bottom:24px;">
        <thead>
        <tr>
            <th field="Name" dataType="String" align="left">Name</th>
            <th field="Spins" dataType="Number" align="right">Spins</th>
            <th field="Accepts" dataType="Number" align="right">Accepts</th>
            <th field="Queued" dataType="Number" align="right">Queued</th>
            <th field="Reads" dataType="Number" align="right">Reads</th>
            <th field="Dropped" dataType="Number" align="right">Dropped</th>
            <th field="Overflow" dataType="Number" align="right">Overflows</th>
            <th field="Timeout" dataType="Number" align="right">Timeouts</th>
            <th field="Socks" dataType="html" align="left" nosort="true">Socks</th>
        </tr>
    	</thead>
    </table>
</div>
