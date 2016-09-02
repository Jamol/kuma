LOCAL_PATH := $(call my-dir)/..
MY_ROOT := $(LOCAL_PATH)/..
include $(CLEAR_VARS)

MY_LIB_PATH = $(MY_ROOT)/vendor/openssl/lib/android

ifeq ($(NDK_DEBUG), 1)
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/armeabi-v7a/debug
else
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/armeabi-v7a/release
endif

LOCAL_MODULE := kuma

LOCAL_SRC_FILES := \
    EventLoopImpl.cpp \
    TcpSocketImpl.cpp \
    UdpSocketImpl.cpp \
    TimerManager.cpp \
    TcpListenerImpl.cpp \
    TcpConnection.cpp \
    poll/EPoll.cpp \
    poll/VPoll.cpp \
    poll/SelectPoll.cpp \
    poll/Notifier.cpp \
    http/Uri.cpp \
    http/HttpParserImpl.cpp \
    http/HttpRequestImpl.cpp \
    http/Http1xRequest.cpp \
    http/HttpResponseImpl.cpp \
    http/v2/H2Frame.cpp \
    http/v2/FrameParser.cpp \
    http/v2/H2Stream.cpp \
    http/v2/Http2Request.cpp \
    http/v2/Http2Response.cpp \
    http/v2/H2ConnectionImpl.cpp \
    http/v2/H2ConnectionMgr.cpp \
    http/v2/hpack/HPackTable.cpp \
    http/v2/hpack/HPacker.cpp \
    ws/WSHandler.cpp \
    ws/WebSocketImpl.cpp \
    util/util.cpp \
    util/kmtrace.cpp \
    util/base64.cpp \
    ssl/SslHandler.cpp \
    ssl/OpenSslLib.cpp \
    kmapi.cpp 

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	$(MY_ROOT)/vendor \
	$(MY_ROOT)/vendor/openssl/include

LOCAL_LDLIBS := -ldl -llog -l$(MY_LIB_PATH)/libssl.a -l$(MY_LIB_PATH)/libcrypto.a
LOCAL_CFLAGS := -w -O2 -D__ANDROID__ -DKUMA_HAS_OPENSSL
LOCAL_CPPFLAGS := -std=c++11
LOCAL_CPP_FEATURES := rtti exceptions

include $(BUILD_SHARED_LIBRARY)
