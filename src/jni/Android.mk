LOCAL_PATH := $(call my-dir)/..
MY_ROOT := $(LOCAL_PATH)/..
include $(CLEAR_VARS)

ifeq ($(NDK_DEBUG), 1)
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/armeabi-v7a/debug
else
	NDK_APP_DST_DIR := $(MY_ROOT)/bin/android/armeabi-v7a/release
endif

LOCAL_MODULE := kuma

LOCAL_SRC_FILES := \
    EventLoopImpl.cpp\
    TcpSocketImpl.cpp\
    UdpSocketImpl.cpp\
    TimerManager.cpp\
    TcpServerSocketImpl.cpp\
    poll/EPoll.cpp\
    poll/VPoll.cpp\
    poll/SelectPoll.cpp\
    http/Uri.cpp\
    http/HttpParserImpl.cpp\
    http/HttpRequestImpl.cpp\
    http/HttpResponseImpl.cpp\
    ws/WSHandler.cpp\
    ws/WebSocketImpl.cpp\
    util/util.cpp\
    util/kmtrace.cpp\
    util/base64.cpp\
    kmapi.cpp\
    jni/kmapi-jni.cpp\
    jni/ws-jni.cpp

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	$(MY_ROOT)/vendor

LOCAL_LDLIBS := -ldl -llog 
LOCAL_CFLAGS := -w -O2 -D__ANDROID__
LOCAL_CPPFLAGS := -std=c++11
LOCAL_CPP_FEATURES := rtti exceptions

include $(BUILD_SHARED_LIBRARY)
