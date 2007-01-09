<% ns_adp_include inc/header.inc %>

<script>
var responseSuccess = function(o) {
    var xml = o.responseXML;
    var clientNodes = xml.getElementsByTagName('client');

    for (var i = 0; i < clientNodes.length; i++) {
        document.write('<br>');
        var childNodes = clientNodes[i].childNodes;

        for (var j = 0; j < childNodes.length; j++) {
            if (childNodes[j].nodeType != 3) {
            document.write(childNodes[j].nodeName + '=' + childNodes[j].childNodes[0].nodeValue + '<br>');
            }
        }
    }
}

var responseFailure = function(o) {
    alert('failure');
}

var callback = 
{
    success:responseSuccess, 
    failure:responseFailure
};

var sUrl = '/tool/ws/stats.xml?option=rpc';
var transaction = YAHOO.util.Connect.asyncRequest('GET', sUrl, callback, null);
</script>

<% ns_adp_include inc/footer.inc %>
