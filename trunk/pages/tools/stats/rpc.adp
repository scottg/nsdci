<% ns_adp_include inc/header.inc %>

<script type="text/javascript">
dojo.require("dojo.io.*")

function getData() {
    dojo.io.bind ({
        url: '/tools/ws/stats.json?option=rpc',
        load: function(type, data, evt) {
            var date = new Date()
            var table = new Table()

            date.setTime(data.timestamp * 1000)

            setText('timestamp', date.toString())
            setText('error', null)

            for (i in data.headers.clients) {
                table.hdr(data.headers.clients[i])
            }

            for (client in data.clients) {
                for (field in data.clients[client]) {
                    if (field == 'socks') {
                        table.col('info') 
                    } else {
                        table.col(data.clients[client][field])
                    }
                }
                 
                table.row()
            }

            table.render('stats') 

            setTimeout('getData()', 1000);
        },
        error: function(type, error) {
            setText('error', 'connection error...' + type)

            setTimeout('getData()', 1000);
        },
        mimetype: "text/json"
    })
}
</script>

<div id='error'></div>
<div id='timestamp'></div>
<div id='stats'></div>

<script>
getData()
</script>

<% ns_adp_include inc/footer.inc %>
