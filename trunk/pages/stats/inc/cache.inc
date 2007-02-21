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
                    var o = {
                        Id:row,
                        Name:data.rows[row]['name'],
                        Entries:parseInt(data.rows[row]['entries']),
                        Flushed:parseInt(data.rows[row]['flushed']),
                        Hits:parseInt(data.rows[row]['hits']),
                        Misses:parseInt(data.rows[row]['misses']),
                        Hitrate:parseInt(data.rows[row]['hitrate'])
                    };
                    
                    statsData.push(o);
                }
                
                var w = dojo.widget.byId("statsTable");
                
                w.store.clearData();
    		    w.store.setData(statsData);
                
                setTimeout('getData()', 1000);
            },
            error: function(type, error) {
                updateError(type, error);
                setTimeout('getData()', 1000);
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
            <th field="Entries" dataType="Number" align="right">Entries</th>
            <th field="Flushed" dataType="Number" align="right">Flushed</th>
            <th field="Hits" dataType="Number" align="right">Hits</th>
            <th field="Misses" dataType="Number" align="right">Misses</th>
            <th field="Hitrate" dataType="Number" align="right">Hitrate</th>
        </tr>
    	</thead>
    </table>
</div>
