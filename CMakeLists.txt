
cmake_minimum_required(VERSION 3.5)
set(CMAKE_VERBOSE_MAKEFILE ON)

project(kuma)

#set(CMAKE_DEBUG_POSTFIX "d")

set(PROJ_ROOT_DIR ${PROJECT_SOURCE_DIR})

set(SIZEOF_VOIDP ${CMAKE_SIZEOF_VOID_P})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH_64 TRUE)
  set(PROCESSOR_ARCH "x64")
  if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(PROCESSOR_ARCH "x86_64")
  endif ()
else()
  set(ARCH_64 FALSE)
  set(PROCESSOR_ARCH "x86")
endif()

include_directories(${PROJ_ROOT_DIR}/include)
include_directories(${PROJ_ROOT_DIR}/src)
include_directories(${PROJ_ROOT_DIR}/third_party)
include_directories(${PROJ_ROOT_DIR}/third_party/openssl/include)

file(GLOB SOURCE_FILES
  ${PROJ_ROOT_DIR}/src/*.cpp
  ${PROJ_ROOT_DIR}/src/utils/*.cpp
  ${PROJ_ROOT_DIR}/src/http/*.cpp
  ${PROJ_ROOT_DIR}/src/http/v2/*.cpp
  ${PROJ_ROOT_DIR}/src/ws/*.cpp
  ${PROJ_ROOT_DIR}/src/ws/exts/*.cpp
  ${PROJ_ROOT_DIR}/src/compr/*.cpp
  ${PROJ_ROOT_DIR}/src/proxy/BasicAuthenticator.cpp
  ${PROJ_ROOT_DIR}/src/proxy/ProxyAuthenticator.cpp
  ${PROJ_ROOT_DIR}/src/proxy/ProxyConnectionImpl.cpp)

file(GLOB SSL_FILES
  ${PROJ_ROOT_DIR}/src/ssl/*.cpp)

file(GLOB ZLIB_FILES 
    ${PROJ_ROOT_DIR}/third_party/zlib/adler32.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/compress.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/crc32.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/deflate.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/infback.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/inffast.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/inflate.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/inftrees.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/trees.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/uncompr.c 
    ${PROJ_ROOT_DIR}/third_party/zlib/zutil.c)
set(SOURCE_FILES ${SOURCE_FILES} ${ZLIB_FILES})

file(GLOB HPACK_FILES 
    ${PROJ_ROOT_DIR}/third_party/HPacker/src/*.cpp)
set(SOURCE_FILES ${SOURCE_FILES} ${HPACK_FILES})

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    include_directories(${PROJ_ROOT_DIR}/third_party/openssl/include)
    file(GLOB WINDOWS_SOURCES 
        #${PROJ_ROOT_DIR}/src/iocp/*.cpp
        ${PROJ_ROOT_DIR}/src/ioop/*.cpp
        ${PROJ_ROOT_DIR}/src/proxy/SspiAuthenticator.cpp)
    
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_darwin\\.cpp$")
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_linux\\.cpp$")

    set(SOURCE_FILES ${SOURCE_FILES} ${WINDOWS_SOURCES} ${SSL_FILES})
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    include_directories(${PROJ_ROOT_DIR}/third_party/openssl/include)
    set(CMAKE_MACOSX_RPATH 1)
    file(GLOB DARWIN_SOURCES 
        ${PROJ_ROOT_DIR}/src/proxy/GssapiAuthenticator.cpp)
    
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_windows\\.cpp$")
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_linux\\.cpp$")

    set(SOURCE_FILES ${SOURCE_FILES} ${DARWIN_SOURCES} ${SSL_FILES})
    if (CMAKE_TARGET_SYSTEM STREQUAL "ios")
    elseif (CMAKE_TARGET_SYSTEM STREQUAL "mac")
    endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    include_directories(/usr/include)
    file(GLOB LINUX_SOURCES 
        ${PROJ_ROOT_DIR}/src/ioop/*.cpp)
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_darwin\\.cpp$")
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_windows\\.cpp$")
    set(SOURCE_FILES ${SOURCE_FILES} ${LINUX_SOURCES} ${SSL_FILES})
elseif (CMAKE_SYSTEM_NAME MATCHES "Android")
    include_directories(${PROJ_ROOT_DIR}/third_party/openssl/include)
    file(GLOB ANDROID_SOURCES 
        ${PROJ_ROOT_DIR}/src/ioop/*.cpp)
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_darwin\\.cpp$")
    list(FILTER SSL_FILES EXCLUDE REGEX ".*_windows\\.cpp$")
    set(SOURCE_FILES ${SOURCE_FILES} ${ANDROID_SOURCES} ${SSL_FILES})
endif ()

add_definitions(-DKUMA_HAS_OPENSSL)
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_definitions(-DKUMA_EXPORTS)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_WINDOWS -D_USRDLL)
    add_definitions(-D_UNICODE -DUNICODE)
    if (ARCH_64)
    else()
        add_definitions(-DWIN32)
    endif()
    if ((CMAKE_BUILD_TYPE STREQUAL "Debug") OR (CMAKE_BUILD_TYPE STREQUAL "debug"))
        add_definitions(-D_DEBUG)
    else ()
        add_definitions(-DNDEBUG)
    endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_definitions(-DLINUX)
elseif (CMAKE_SYSTEM_NAME MATCHES "Android")
    add_definitions(-DANDROID -DLINUX)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    option(WIN_USE_MD "Use DLL-specific version of multithread runtime library (/MD)" OFF)
    #message(STATUS "++++++++ flags=${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    if (WIN_USE_MD)
        message("Using DLL-specific version of multithread runtime library (/MD)")
        set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0")
        set(CMAKE_CXX_FLAGS_DEBUG "/MDd /D_DEBUG /Zi /Ob0 /Od /RTC1 -D_DEBUG -DDYNAMIC_ANNOTATIONS_ENABLED=1 -DWTF_USE_DYNAMIC_ANNOTATIONS=1")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /O2 /Zi -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0")
    else ()
        message("Using static version of multithread runtime library (/MT)")
        set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0")
        set(CMAKE_CXX_FLAGS_DEBUG "/MTd /D_DEBUG /Zi /Ob0 /Od /RTC1 -D_DEBUG -DDYNAMIC_ANNOTATIONS_ENABLED=1 -DWTF_USE_DYNAMIC_ANNOTATIONS=1")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /O2 /Zi -DNDEBUG -DNVALGRIND -DDYNAMIC_ANNOTATIONS_ENABLED=0")
    endif ()
    if (ARCH_64)
        link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/windows/x64/${CMAKE_BUILD_TYPE})
        link_directories(${PROJ_ROOT_DIR}/third_party/openssl/lib/windows/x64)
    else ()
        link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/windows/Win32/${CMAKE_BUILD_TYPE})
        link_directories(${PROJ_ROOT_DIR}/third_party/openssl/lib/windows/Win32)
    endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -std=c++14 -stdlib=libc++")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden -Wnon-virtual-dtor -Woverloaded-virtual -fPIC")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
    if (CMAKE_TARGET_SYSTEM STREQUAL "ios")
        if ((CMAKE_OSX_ARCHITECTURES STREQUAL "armv7") OR (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
        	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fembed-bitcode")
        	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fembed-bitcode")
        endif ()
        link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/ios/${CMAKE_BUILD_TYPE}-${XCODE_IOS_PLATFORM})
        link_directories(${PROJ_ROOT_DIR}/third_party/openssl/lib/ios/${XCODE_IOS_PLATFORM})
    elseif (CMAKE_TARGET_SYSTEM STREQUAL "mac")
        link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/mac/${CMAKE_BUILD_TYPE})
        link_directories(${PROJ_ROOT_DIR}/third_party/openssl/lib/mac/${CMAKE_OSX_ARCHITECTURES})
    endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=c++14")
    link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/linux/${PROCESSOR_ARCH}/${CMAKE_BUILD_TYPE})
elseif (CMAKE_SYSTEM_NAME MATCHES "Android")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fvisibility-inlines-hidden")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor -Woverloaded-virtual -fexceptions")
    link_directories(${PROJ_ROOT_DIR}/third_party/libkev/lib/android/${CMAKE_ANDROID_ARCH_ABI})
    link_directories(${PROJ_ROOT_DIR}/third_party/openssl/lib/android/${CMAKE_ANDROID_ARCH_ABI})
endif ()

# ios target definition
if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if (CMAKE_TARGET_SYSTEM STREQUAL "ios")
        set(IOS_DEPLOYMENT_TARGET "11.0")
        set(XCODE_IOS_PLATFORM_VERSION_FLAG "-m${XCODE_IOS_PLATFORM}-version-min=${IOS_DEPLOYMENT_TARGET}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${XCODE_IOS_PLATFORM_VERSION_FLAG}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${XCODE_IOS_PLATFORM_VERSION_FLAG}")
    elseif (CMAKE_TARGET_SYSTEM STREQUAL "mac")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13)
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    file(GLOB API_HEADERS ${PROJ_ROOT_DIR}/include/*.h)
    if (CMAKE_TARGET_SYSTEM STREQUAL "ios")
        add_library(kuma SHARED ${SOURCE_FILES} ${API_HEADERS})
        set_target_properties(kuma PROPERTIES
                              FRAMEWORK TRUE
                              FRAMEWORK_VERSION A
                              PUBLIC_HEADER "${API_HEADERS}"
                              MACOSX_FRAMEWORK_IDENTIFIER org.jamol.kuma)
    else ()
        add_library(kuma SHARED ${SOURCE_FILES} ${API_HEADERS})
        set_target_properties(kuma PROPERTIES 
            FRAMEWORK TRUE
            MACOSX_RPATH TRUE
            PUBLIC_HEADER "${API_HEADERS}"
            MACOSX_FRAMEWORK_IDENTIFIER org.jamol.kuma
        )
    endif ()
else ()
    add_library(kuma SHARED ${SOURCE_FILES})
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_options(kuma PRIVATE /W3 /WX)
else ()
    target_compile_options(kuma PRIVATE -Werror)
endif ()


if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    if (ARCH_64)
        set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/windows/${PROCESSOR_ARCH}/${CMAKE_BUILD_TYPE}")
    else ()
        set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/windows/Win32/${CMAKE_BUILD_TYPE}")
    endif ()
    target_link_libraries(kuma kev.lib libcrypto.lib libssl.lib ws2_32.lib)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if (CMAKE_TARGET_SYSTEM STREQUAL "ios")
        set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/ios/${CMAKE_BUILD_TYPE}-${XCODE_IOS_PLATFORM}")
        target_link_libraries(kuma kev crypto ssl "-framework GSS")
    elseif (CMAKE_TARGET_SYSTEM STREQUAL "mac")
        set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/mac/${CMAKE_BUILD_TYPE}")
        target_link_libraries(kuma kev crypto.1.1 ssl.1.1 "-framework GSS")
        target_link_libraries(kuma "-framework CoreFoundation")
        target_link_libraries(kuma "-framework Security")
    endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/linux/${PROCESSOR_ARCH}/${CMAKE_BUILD_TYPE}")
    target_link_libraries(kuma kev crypto.1.1 ssl.1.1)
elseif (CMAKE_SYSTEM_NAME MATCHES "Android")
    set(LIBRARY_OUTPUT_PATH "${PROJ_ROOT_DIR}/bin/android/${CMAKE_ANDROID_ARCH_ABI}")
    target_link_libraries(kuma kev crypto.1.1 ssl.1.1)
endif ()
