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
                    var args = data.rows[row]['args'];
                    var proc = data.rows[row]['proc'];
                     
                    var connId = "";
                    var peer = "";
                    var state = "";
                    var method = "";
                    var url = "";
                    var elapsed = "";
                    var sent = "";
                    
                    if (proc == "ns:connthread") {
                        if (args['connId'] != "") {
                            connId = parseInt(args['connId']);
                            peer = args['peer'];
                            state = args['state'];
                            method = args['method'];
                            url = args['url'];
                            elapsed = parseFloat(args['elapsed']);
                            sent = parseInt(args['sent']);
                        } 
                        
                        args = "";
                    }
                 
                    var o = {
                        Id:row,
                        Name:data.rows[row]['name'],
                        Parent:data.rows[row]['parent'],
                        Tid:parseInt(data.rows[row]['tid']),
                        Flags:data.rows[row]['flags'],
                        Ctime:parseInt(data.rows[row]['ctime']),
                        Proc:proc,
                        Args:args,
                        ConnId:connId,
                        Peer:peer,
                        State:state,
                        Method:method,
                        Url:url,
                        Elapsed:elapsed,
                        Sent:sent
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
            <th field="Parent" dataType="String" align="left">Parent</th>
            <th field="Tid" dataType="Number" align="right">Tid</th>
            <th field="Flags" dataType="String" align="left">Flags</th>
            <th field="Ctime" dataType="Number" align="right">Created</th>
            <th field="Proc" dataType="String" align="left">Proc</th>
            <th field="Args" dataType="String" align="left">Args</th>
            <th field="ConnId" dataType="Number" align="right">Cid</th>
            <th field="Peer" dataType="Number" align="right">Peer</th>
            <th field="State" dataType="String" align="left">State</th>
            <th field="Elapsed" dataType="Number" align="right">Elapsed</th>
            <th field="Sent" dataType="Number" align="right">Sent</th>
            <th field="Method" dataType="String" align="left">Method</th>
            <th field="Url" dataType="String" align="left">Url</th>
        </tr>
    	</thead>
    </table>
</div>
