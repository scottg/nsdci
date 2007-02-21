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
                        Nsockerror:parseInt(data.rows[row]['nsockerror']),
                        Nsend:parseInt(data.rows[row]['nsend']),
                        Tmin:parseInt(data.rows[row]['tmin']),
                        Tavg:parseInt(data.rows[row]['tavg']),
                        Tmax:parseInt(data.rows[row]['tmax']),
                        Nsockwait:parseInt(data.rows[row]['nsockwait']),
                        Nsendwait:parseInt(data.rows[row]['nsendwait']),
                        Nnosocks:parseInt(data.rows[row]['nnosocks']),
                        Nrecv:parseInt(data.rows[row]['nrecv']),
                        Nrecvwait:parseInt(data.rows[row]['nrecvwait']),
                        Nsocktimeout:parseInt(data.rows[row]['nsocktimeout']),
                        Nreqtimeout:parseInt(data.rows[row]['nreqtimeout']),
                        Nrequest:parseInt(data.rows[row]['nrequest'])
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
            <th field="Name" dataType="String">Name</th>
            <th field="Nsockerror" dataType="Number" align="right">Sock Errs</th>
            <th field="Nsend" dataType="Number" align="right">Sends</th>
            <th field="Tmin" dataType="Number" align="right">Min Time</th>
            <th field="Tavg" dataType="Number" align="right">Avg Time</th>
            <th field="Tmax" dataType="Number" align="right">Max Time</th>
            <th field="Nsockwait" dataType="Number" align="right">Sock Waits</th>
            <th field="Nsendwait" dataType="Number" align="right">Send Waits</th>
            <th field="Nnosocks" dataType="Number" align="right">No Socks</th>
            <th field="Nrecv" dataType="Number" align="right">Recvs</th>
            <th field="Nrecvwait" dataType="Number" align="right">Recv Waits</th>
            <th field="Nsocktimeout" dataType="Number" align="right">Sock Timeouts</th>
            <th field="Nreqtimeout" dataType="Number" align="right">Req Timeouts</th>
            <th field="Nrequest" dataType="Number" align="right">Requests</th>
        </tr>
    	</thead>
    </table>
</div>
