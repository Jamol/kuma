# kuma test client
a test client for kuma library

# usage
```
  client [option] tcp://127.0.0.1:52328
                  udp://127.0.0.1:52328
                  mcast//224.0.0.1:52328
                  http://127.0.0.1:8443
                  https://127.0.0.1:8443
                  ws://127.0.0.1:8443
                  wss://127.0.0.1:8443

  mcast: test Multicast

  options:
    -b host:port    #local host and port to be bound to
    -c number       #concurrent clients
    -t ms           #data sending interval
    -v              #print version
    --http2         #test http2, only valid for http/https
```

# examples
```
  $ client https://www.google.com --http2
  $ client ws://127.0.0.1:8443 -c 100 -t 1000
```


