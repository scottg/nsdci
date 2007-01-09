<% ns_adp_include inc/header.inc %>

<script type="text/javascript">
dojo.require("dojo.io.*")
dojo.require("dojo.xml.*")

function getData() {
    dojo.io.bind ({
        url: '/tool/ws/stats.json?option=rpc',
        load: function(type, data, evt) {
            var errorNode = document.getElementById('error')
            errorNode.innerHTML = ''

            var statsNode = document.getElementById('stats')  
            statsNode.innerHTML = ''

            for (client in data.clients) {
                var srNode = document.createElement('s_r')

                for (field in data.clients[client]) {
                    var scNode = document.createElement('s_c')
                    var value = data.clients[client][field]

                    if (field == 'socks') {
                        var sockStNode = document.createElement('s_t')

                        for (j = 0; j < value.length; j++) {
                            var sockSrNode = document.createElement('s_r')

                            for (sockField in value[j]) {
                                var sockValue = value[j][sockField]
                                var sockScNode = document.createElement('s_c')
                
                                sockScNode.appendChild(document.createTextNode(sockValue)) 
                                sockSrNode.appendChild(sockScNode)
                            }

                            sockStNode.appendChild(sockSrNode)
                        }
                         
                        scNode.appendChild(sockStNode)    
                    } else {
                        scNode.appendChild(document.createTextNode(value))
                    }

                    srNode.appendChild(scNode)
                }

                statsNode.appendChild(srNode)
            } 

            setTimeout('getData()', 1000);
        },
        error: function(type, error) {
            var errorNode = document.getElementById('error')
            errorNode.innerHTML = 'connection error...'

            setTimeout('getData()', 1000);
        },
        mimetype: "text/json"
    })
}

getData()
</script>

<div id='error'></div>

<s_t id='stats'></s_t>

<% ns_adp_include inc/footer.inc %>
