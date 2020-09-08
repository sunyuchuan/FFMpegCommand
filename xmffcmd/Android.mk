LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CFLAGS += -mfloat-abi=soft
endif
ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_CFLAGS += -marm
endif
ifeq ($(TARGET_ARCH_ABI),x86)
LOCAL_CFLAGS += -march=atom -msse3 -ffast-math -mfpmath=sse
endif

LOCAL_CFLAGS += -std=c99
LOCAL_CFLAGS += -Wno-deprecated-declarations -w
LOCAL_LDLIBS += -llog -landroid

LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(realpath $(LOCAL_PATH)/..)
LOCAL_C_INCLUDES += $(MY_APP_FFMPEG_INCLUDE_PATH)
LOCAL_C_INCLUDES += $(realpath $(LOCAL_PATH)/../ijkj4a)
LOCAL_C_INCLUDES += $(realpath $(LOCAL_PATH)/../ijkplayer)

LOCAL_SRC_FILES := xm_ffmpeg_command_jni.c \
                   xm_ffmpeg_command.c \
                   xm_adts_utils.c \
                   cmdutils.c \
                   ffmpeg_filter.c \
                   ffmpeg_opt.c \
                   ffmpeg.c

LOCAL_SHARED_LIBRARIES := ijkffmpeg ijksdl

LOCAL_MODULE := xmffcmd

include $(BUILD_SHARED_LIBRARY)
