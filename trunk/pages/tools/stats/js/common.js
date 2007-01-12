function Table() {
    var tableNode = document.createElement('s_t')
    var rowNode = null
    var headerRowNode = null
    var class = 'even'

    function addCol(text) {
        if (headerRowNode != null) {
            tableNode.appendChild(headerRowNode)
            headerRowNode = null
        }

        if (rowNode == null) {
            rowNode = document.createElement('s_r')
        } 

        colNode = document.createElement('s_c')    
        colNode.appendChild(document.createTextNode(text))
        colNode.className = class
        rowNode.appendChild(colNode)
     }

     function addHdr(text) {
         if (headerRowNode == null) {
             headerRowNode = document.createElement('s_h')
         }

         headerNode = document.createElement('s_l') 
         headerNode.appendChild(document.createTextNode(text))
         headerRowNode.appendChild(headerNode)
     }

     function addRow() {
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

     function display(id) {
         node = document.getElementById(id)
         node.appendChild(tableNode)
     }

     this.addHdr = addHdr
     this.addCol = addCol
     this.addRow = addRow
     this.display = display
}
