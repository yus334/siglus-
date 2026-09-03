LOCAL_PATH := $(call my-dir)
MY_JNI := $(LOCAL_PATH)

# ---- SDL2（官方 Android.mk 定义 SDL2 共享库、SDL2main 静态库、hidapi）----
SDL_ROOT := $(MY_JNI)/../../../../thirdparty/SDL
include $(SDL_ROOT)/Android.mk

# ---- 我们的引擎 ----
LOCAL_PATH := $(MY_JNI)
include $(CLEAR_VARS)
LOCAL_MODULE := siglus

LOCAL_CFLAGS := -DSIGLUS_ANDROID=1 -O2 -Wall \
    -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter
LOCAL_CPPFLAGS := -std=c++17 -frtti -fexceptions

LOCAL_C_INCLUDES := \
    $(MY_JNI)/../../../../src \
    $(SDL_ROOT)/include

LOCAL_SRC_FILES := \
    $(MY_JNI)/../../../../src/core/FileSystem.cpp \
    $(MY_JNI)/../../../../src/archive/Archive.cpp \
    $(MY_JNI)/../../../../src/media/Decoder.cpp \
    $(MY_JNI)/../../../../src/script/Script.cpp \
    $(MY_JNI)/../../../../src/gfx/Renderer.cpp \
    $(MY_JNI)/../../../../src/text/Text.cpp \
    $(MY_JNI)/../../../../src/audio/Audio.cpp \
    $(MY_JNI)/../../../../src/input/Input.cpp \
    $(MY_JNI)/../../../../src/app/Engine.cpp \
    $(MY_JNI)/../../../../src/app/main.cpp \
    $(MY_JNI)/../../../../src/platform/VideoPlayer_android.cpp

LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_STATIC_LIBRARIES := SDL2main
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lz

# stb 可选：存在才启用图像/字体/OGG 解码
STB_ROOT := $(MY_JNI)/../../../../thirdparty/stb
ifneq ($(wildcard $(STB_ROOT)/stb_image.h),)
  LOCAL_C_INCLUDES += $(STB_ROOT)
  LOCAL_CPPFLAGS += -DSIGLUS_HAVE_STB=1 -DSIGLUS_HAVE_STB_IMAGE=1 -DSIGLUS_HAVE_STB_TRUETYPE=1 -DSIGLUS_HAVE_STB_VORBIS=1
else
  LOCAL_CPPFLAGS += -DSIGLUS_HAVE_STB=0
endif

include $(BUILD_SHARED_LIBRARY)
