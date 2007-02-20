function displayTimestamp (data) {
    if (!data.timestamp) {
        return
    }

    var date = new Date();
    date.setTime(data.timestamp * 1000);
    setText('timestamp', date.toString())
}

function displayStatsTable (data) {
    if (!data.headers) {
        return
    }

    var table = new Table();

    for (i in data.headers) {
        table.hdr(data.headers[i])
    }

    for (row in data.rows) {
        for (field in data.rows[row]) {
            table.col(data.rows[row][field])
        }

        table.row()
    }

    table.render('stats');
}

function displayStatsError(type, error) {
    error = dojo.errorToString(error);
    setText('error', error);
}

function displayStatsMessage(data) {
    if (!data.message) {
        return
    }
    setText('message', data.message);
}

function Table() {
    var tableNode = document.createElement('s_t')
    var rowNode = null
    var headerRowNode = null
    var columnNode = null
    var headerNode = null
    var className = 'even'

    function col(text) {
        if (headerRowNode != null) {
            tableNode.appendChild(headerRowNode)
            headerRowNode = null
        }

        if (rowNode == null) {
            rowNode = document.createElement('s_r')
        } 

        columnNode = document.createElement('s_c')    
        columnNode.appendChild(document.createTextNode(text))
        columnNode.className = className
        rowNode.appendChild(columnNode)
     }

     function hdr(text) {
         if (headerRowNode == null) {
             headerRowNode = document.createElement('s_h')
         }

         headerNode = document.createElement('s_l') 
         headerNode.appendChild(document.createTextNode(text))
         headerRowNode.appendChild(headerNode)
     }

     function row() {
         if (rowNode != null) {
             if (className == 'even') {
                 className = 'odd'
             } else {
                 className = 'even'
             }

             tableNode.appendChild(rowNode)
             rowNode = null      
         }
     }

     function render(id) {
         node = document.getElementById(id)

         node.innerHTML = null
         node.appendChild(tableNode)
     }

     // Properties.

     this.tableNode = tableNode
     this.columnNode = columnNode
     this.rowNode = rowNode
     this.headerRowNode = headerRowNode
     this.headerNode = headerNode

     // Methods.

     this.hdr = hdr
     this.col = col
     this.row = row
     this.render = render
}

function setText(id, text) {
    var node = document.getElementById(id)

    node.innerHTML = text
}
