MY_ROOT := $(call my-dir)/../..
APP_MODULES := kuma

ifeq ($(NDK_DEBUG), 1)
	APP_OPTIM := debug
	NDK_APP_OUT := $(MY_ROOT)/objs/android/debug
else
	APP_OPTIM := release
	NDK_APP_OUT := $(MY_ROOT)/objs/android/release
endif

APP_PLATFORM := android-19
APP_ABI := armeabi-v7a
APP_STL := c++_shared
