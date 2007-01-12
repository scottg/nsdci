function Table() {
    var tableNode = document.createElement('s_t')
    var rowNode = null
    var headerRowNode = null
    var columnNode = null
    var headerNode = null
    var class = 'even'

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
        columnNode.className = class
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
             if (class == 'even') {
                 class = 'odd'
             } else {
                 class = 'even'
             }

             tableNode.appendChild(rowNode)
             rowNode = null      
         }
     }

     function render(id) {
         node = document.getElementById(id)
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
