<%
set action [ns_queryget a]
%>

<!doctype html public "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
    <title></title>
    <link rel="stylesheet" type="text/css" href="css/global.css">
</head>
<body>
<h1>Network Polls</h1>

<%
switch $action {
    "graph" {
        set poll [ns_queryget p]

        ns_adp_include inc/np_graph.inc $poll
    }
    "save" {
        set poll [ns_queryget p]
        set form [ns_conn form]

        set d [nps.vote $poll]

        array set b [ns_set array $form]

        foreach key [array names b] {
           if {[string match "b_*" $key]} {
               set bucket [lindex [split $key "_"] 1]
               set currentValue [lindex $d $bucket]
               set newValue $b($key)
               set adjustment [expr $newValue - $currentValue]

               nps2.vote $poll [expr $bucket - 1] $adjustment
           }
        }

        ns_adp_include inc/np.inc
    }
    "edit" {
        set poll [ns_queryget p]

        ns_adp_include inc/np_edit.inc $poll
    }
    "delete" {
        set poll [ns_queryget p]
        
        nps.delete $poll

        if {![string length [nps.find]]} {
            ns_adp_puts "There are no polls currently defined."
        } else {
            ns_adp_include inc/np.inc
        }
    }
    default {
        if {![string length [nps.find]]} {
            ns_adp_puts "There are no polls currently defined."
        } else {
            ns_adp_include inc/np.inc
        }
    }
}
%>

</body>
</html>
