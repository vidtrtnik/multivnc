LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := libvncclient
LOCAL_SRC_FILES := lib/libvncclient.so
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/rfb
include $(PREBUILT_SHARED_LIBRARY)