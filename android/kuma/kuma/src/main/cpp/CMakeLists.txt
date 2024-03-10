# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("kuma")
set(PROJ_ROOT_DIR ${PROJECT_SOURCE_DIR}/../../../../../..)

set(OPENSSL_PATH ${PROJ_ROOT_DIR}/third_party/openssl)
set(OPENSSL_LIB_PATH ${OPENSSL_PATH}/lib/android/${ANDROID_ABI})
set(LIBKEV_PATH ${PROJ_ROOT_DIR}/third_party/libkev)
set(LIBKEV_LIB_PATH ${LIBKEV_PATH}/lib/android/${ANDROID_ABI})
set(HPACK_PATH ${PROJ_ROOT_DIR}/third_party/HPacker)
set(ZLIB_PATH ${PROJ_ROOT_DIR}/third_party/zlib)

file(GLOB SOURCE_FILES ${PROJECT_SOURCE_DIR}/*.cpp
        ${PROJECT_SOURCE_DIR}/utils/*.cpp
        ${PROJECT_SOURCE_DIR}/utils/*.cc)

set(SOURCE_FILES ${SOURCE_FILES}
        ${PROJECT_SOURCE_DIR}/utils/jvm.cc
        ${PROJECT_SOURCE_DIR}/utils/scoped_java_ref.cc
        )

file(GLOB KUMA_SOURCE_FILES ${PROJ_ROOT_DIR}/src/*.cpp
        ${PROJ_ROOT_DIR}/src/compr/*.cpp
        ${PROJ_ROOT_DIR}/src/http/*.cpp
        ${PROJ_ROOT_DIR}/src/http/v2/*.cpp
        ${PROJ_ROOT_DIR}/src/proxy/BasicAuthenticator.cpp
        ${PROJ_ROOT_DIR}/src/proxy/ProxyAuthenticator.cpp
        ${PROJ_ROOT_DIR}/src/proxy/ProxyConnectionImpl.cpp
        ${PROJ_ROOT_DIR}/src/ssl/BioHandler.cpp
        ${PROJ_ROOT_DIR}/src/ssl/OpenSslLib.cpp
        ${PROJ_ROOT_DIR}/src/ssl/SioHandler.cpp
        ${PROJ_ROOT_DIR}/src/ssl/ssl_utils_linux.cpp
        ${PROJ_ROOT_DIR}/src/ssl/SslHandler.cpp
        ${PROJ_ROOT_DIR}/src/utils/*.cpp
        ${PROJ_ROOT_DIR}/src/ws/*.cpp
        ${PROJ_ROOT_DIR}/src/ws/exts/*.cpp
        ${PROJ_ROOT_DIR}/src/ioop/*.cpp
        )
set(SOURCE_FILES ${SOURCE_FILES} ${KUMA_SOURCE_FILES})


file(GLOB ZLIB_SOURCE_FILES ${ZLIB_PATH}/adler32.c
        ${ZLIB_PATH}/adler32.c
        ${ZLIB_PATH}/compress.c
        ${ZLIB_PATH}/crc32.c
        ${ZLIB_PATH}/deflate.c
        ${ZLIB_PATH}/infback.c
        ${ZLIB_PATH}/inffast.c
        ${ZLIB_PATH}/inflate.c
        ${ZLIB_PATH}/inftrees.c
        ${ZLIB_PATH}/trees.c
        ${ZLIB_PATH}/uncompr.c
        ${ZLIB_PATH}/zutil.c
        )
set(SOURCE_FILES ${SOURCE_FILES} ${ZLIB_SOURCE_FILES})

file(GLOB HPACK_SOURCE_FILES ${HPACK_PATH}/src/HPackTable.cpp
        ${HPACK_PATH}/src/HPacker.cpp
        )
set(SOURCE_FILES ${SOURCE_FILES} ${HPACK_SOURCE_FILES})


include_directories(${PROJECT_SOURCE_DIR})

include_directories(${PROJ_ROOT_DIR}/include)
include_directories(${PROJ_ROOT_DIR}/src)
include_directories(${PROJ_ROOT_DIR}/third_party)
include_directories(${OPENSSL_PATH}/include)

add_definitions(-DANDROID -D__ANDROID__ -DKUMA_HAS_OPENSSL)

link_directories(${LIBKEV_LIB_PATH})
link_directories(${OPENSSL_LIB_PATH})
# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.
#
# In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
# the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
# is preferred for the same purpose.
#
# In order to load a library into your app from Java/Kotlin, you must call
# System.loadLibrary() and pass the name of the library defined here;
# for GameActivity/NativeActivity derived applications, the same library name must be
# used in the AndroidManifest.xml file.
add_library(${CMAKE_PROJECT_NAME} SHARED
        # List C/C++ source files with relative paths to this CMakeLists.txt.
        ${SOURCE_FILES})

# Specifies libraries CMake should link to your target library. You
# can link libraries from various origins, such as libraries defined in this
# build script, prebuilt third-party libraries, or Android system libraries.
target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        log
        kev
        ssl.1.1
        crypto.1.1)