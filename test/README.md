# test client
##### test HTTP
```
./client https://www.cloudflare.com/ --http2
./client https://http2.golang.org/reqinfo --http2 -c 5
./client http://google.com/
```

##### test IPv6
```
./client https://[::1]:8443/
```

# test server
server will load the test HTML and scripts in [test/www](https://github.com/Jamol/kuma/tree/master/test/www), you can visit the test site from browser

##### test HTTP only
```
./server http://0.0.0.0:8443/
./server https://0.0.0.0:8443/
./server https://[::]:8443/
```

##### test WebSocket only
```
./server ws://0.0.0.0:8443/
./server wss://0.0.0.0:8443/
./server wss://[::]:8443/
```

##### demultiplexing protocol automatically
```
./server auto://0.0.0.0:8443/
./server autos://0.0.0.0:8443/
./server autos://[::]:8443/
```

