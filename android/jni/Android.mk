#
#
TOP_LOCAL_PATH := $(call my-dir)
include $(call all-subdir-makefiles)

LOCAL_PATH := $(TOP_LOCAL_PATH)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/prebuilt-libs/include/rfb
LOCAL_MODULE    := jnivncconn
LOCAL_SRC_FILES := JNIVNCConn.cpp
LOCAL_SHARED_LIBRARIES := libvncclient
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 

include $(BUILD_SHARED_LIBRARY)
