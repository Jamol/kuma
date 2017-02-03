# kuma test server
a test server for kuma library

# usage
  server tcp://0.0.0.0:52328
         udp://0.0.0.0:52328
         http://0.0.0.0:8443
         https://0.0.0.0:8443
         ws://0.0.0.0:8443
         wss://0.0.0.0:8443
         auto://0.0.0.0:8443
         autos://0.0.0.0:8443

  auto(s): demultiplexing WebSocket, HTTP, HTTP2 automatically

# example
  $ server autos://0.0.0.0:8443
