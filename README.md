# kuma
kuma is a multi-platform support network library developed in C++11. It implements interfaces for TCP/UDP/Multicast/HTTP/HTTP2/WebSocket/timer that drove by event loop. kuma supports epoll/poll/WSAPoll/kqueue/select on platform Linux/Windows/OSX/iOS/Android.


## Build
```
define macro KUMA_HAS_OPENSSL to enable openssl
```

### iOS
```
open project bld/ios/kuma with xcode and build it
```

### OSX
```
open project bld/osx/kuma with xcode and build it
```

### Windows
```
open bld/msvc/kuma.sln with VS2015 and build it
```

### Linux
```
$ cd src
$ make
```

### Android
```
$ cd src/jni
$ ndk-build
```

## OpenSSL
```
certificates location is same as your binary folder by default.

copy all CA certificates used to ca.cer
copy your server certificate to server.cer
copy your server private key to server.key
```

