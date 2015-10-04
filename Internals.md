# SOB via HTTP #
## RPC Commands ##
```
 READ      1
 PUT       2
 UNLINK    3
 COPY      4
 APPEND    5
 CBPOST    10
 CBDEL     11
```
## HTTP Message Structure ##
```
 1: <METHOD> /<RPC NAME>/<RPC COMMAND> HTTP/1.0\r\n
 2: host: <HOST>\r\n
 3: content-length: <RPC ARGUMENTS LENGTH>\r\n
 4: connection: <CLOSE|KEEP-ALIVE>\r\n
 5: \r\n
 6: <RPC ARGUMENTS>
```