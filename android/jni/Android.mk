#
#
TOP_LOCAL_PATH := $(call my-dir)
include $(call all-subdir-makefiles)

LOCAL_PATH := $(TOP_LOCAL_PATH)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/prebuilt-libs/include/
LOCAL_MODULE    := vncconn
LOCAL_SRC_FILES := vncconn.c
LOCAL_SHARED_LIBRARIES := libvncclient
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 

include $(BUILD_SHARED_LIBRARY)
