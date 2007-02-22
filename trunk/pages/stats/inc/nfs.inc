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
                        Nerror:parseInt(data.rows[row]['nerror']),
                        Ncbdel:parseInt(data.rows[row]['ncbdel']),
                        Ncbpost:parseInt(data.rows[row]['ncbpost']),
                        Ncopy:parseInt(data.rows[row]['ncopy']),
                        Nwrite:parseInt(data.rows[row]['nwrite']),
                        Nappend:parseInt(data.rows[row]['nappend']),
                        Nreadnoent:parseInt(data.rows[row]['nreadnoent'])
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
            <th field="Ncbdel" dataType="Number" align="right">NCB Deletes</th>
            <th field="Ncbpost" dataType="Number" align="right">NCB Posts</th>
            <th field="Nreadnoent" dataType="Number" align="right">NCB Creates</th>
            <th field="Ncopy" dataType="Number" align="right">Copies</th>
            <th field="Nwrite" dataType="Number" align="right">Writes</th>
            <th field="Nappend" dataType="Number" align="right">Appends</th>
            <th field="Nerror" dataType="Number" align="right">Errors</th>
        </tr>
    	</thead>
    </table>
</div>
