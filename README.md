# kuma


## Build
``
define macro KUMA_HAS_OPENSSL to enable openssl
``

### iOS
``
open project bld/ios/kuma with xcode and build it
``

### OSX
``
open project bld/osx/kuma with xcode and build it
``

### Windows
``
open bld/msvc/kuma.sln with VS2015 and build it
``

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
``
certificates location is same as your binary by default.
``
copy all CA certificates used to ca.cer
copy your server certificate to server.cer
copy your server private key to server.key
``