LOCAL_PATH := $(call my-dir)/..
MY_ROOT := $(LOCAL_PATH)/..
include $(CLEAR_VARS)

OPENSSL_PATH := $(MY_ROOT)/third_party/openssl
OPENSSL_LIB_PATH := $(OPENSSL_PATH)/lib/android/$(APP_ABI)
LIBKEV_PATH := $(MY_ROOT)/third_party/libkev
LIBKEV_LIB_PATH := $(LIBKEV_PATH)/lib/android/$(APP_ABI)
HPACK_PATH := $(MY_ROOT)/third_party/HPacker

ifeq ($(NDK_DEBUG), 1)
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/$(APP_ABI)/debug
else
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/$(APP_ABI)/release
endif

LOCAL_MODULE := kuma

MY_ZLIB_DIR := ../third_party/zlib

MY_ZLIB_SRC_FILES := \
    $(MY_ZLIB_DIR)/adler32.c \
    $(MY_ZLIB_DIR)/compress.c \
    $(MY_ZLIB_DIR)/crc32.c \
    $(MY_ZLIB_DIR)/deflate.c \
    $(MY_ZLIB_DIR)/infback.c \
    $(MY_ZLIB_DIR)/inffast.c \
    $(MY_ZLIB_DIR)/inflate.c \
    $(MY_ZLIB_DIR)/inftrees.c \
    $(MY_ZLIB_DIR)/trees.c \
    $(MY_ZLIB_DIR)/uncompr.c \
    $(MY_ZLIB_DIR)/zutil.c

LOCAL_SRC_FILES := \
    AcceptorBase.cpp \
    SocketBase.cpp \
    UdpSocketBase.cpp \
    TcpSocketImpl.cpp \
    UdpSocketImpl.cpp \
    TcpListenerImpl.cpp \
    TcpConnection.cpp \
    http/Uri.cpp \
    http/HttpHeader.cpp \
    http/HttpMessage.cpp \
    http/HttpParserImpl.cpp \
    http/H1xStream.cpp \
    http/HttpRequestImpl.cpp \
    http/Http1xRequest.cpp \
    http/HttpResponseImpl.cpp \
    http/Http1xResponse.cpp \
    http/HttpCache.cpp \
    http/httputils.cpp \
    http/v2/H2Frame.cpp \
    http/v2/FrameParser.cpp \
    http/v2/FlowControl.cpp \
    http/v2/H2Handshake.cpp \
    http/v2/H2Stream.cpp \
    http/v2/H2StreamProxy.cpp \
    http/v2/Http2Request.cpp \
    http/v2/Http2Response.cpp \
    http/v2/H2ConnectionImpl.cpp \
    http/v2/H2ConnectionMgr.cpp \
    http/v2/h2utils.cpp \
    http/v2/PushClient.cpp \
    ${HPACK_PATH}/src/HPackTable.cpp \
    ${HPACK_PATH}/src/HPacker.cpp \
    compr/compr.cpp \
    compr/compr_zlib.cpp \
    ws/WSHandler.cpp \
    ws/WebSocketImpl.cpp \
    ws/WSConnection.cpp \
    ws/WSConnection_v1.cpp \
    ws/WSConnection_v2.cpp \
    ws/exts/ExtensionHandler.cpp \
    ws/exts/PMCE_Deflate.cpp \
    ws/exts/PMCE_Base.cpp \
    ws/exts/WSExtension.cpp \
    utils/utils.cpp \
    utils/base64.cpp \
    ssl/SslHandler.cpp \
    ssl/BioHandler.cpp \
    ssl/SioHandler.cpp \
    ssl/OpenSslLib.cpp \
    ssl/ssl_utils_linux.cpp \
    proxy/ProxyAuthenticator.cpp \
    proxy/BasicAuthenticator.cpp \
    proxy/ProxyConnectionImpl.cpp \
    DnsResolver.cpp \
    kmapi.cpp

LOCAL_SRC_FILES += $(MY_ZLIB_SRC_FILES)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
    $(MY_ROOT)/include \
	$(MY_ROOT)/third_party \
	$(OPENSSL_PATH)/include

LOCAL_LDLIBS := -ldl -llog -l$(LIBKEV_LIB_PATH)/libkev.a -l$(OPENSSL_LIB_PATH)/libssl.1.1.so -l$(OPENSSL_LIB_PATH)/libcrypto.1.1.so
LOCAL_CFLAGS := -w -O2 -D__ANDROID__ -DKUMA_HAS_OPENSSL
LOCAL_CPPFLAGS := -std=c++14
LOCAL_CPP_FEATURES := rtti exceptions

include $(BUILD_SHARED_LIBRARY)
