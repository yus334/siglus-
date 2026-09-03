LOCAL_PATH := $(call my-dir)
MY_JNI := $(LOCAL_PATH)

# 从 jni/ 到仓库根需要 5 级上级目录：
#   android/app/src/main/jni -> main -> src -> app -> android -> 仓库根
SDL_ROOT := $(MY_JNI)/../../../../../thirdparty/SDL
include $(SDL_ROOT)/Android.mk

# ---- 我们的引擎 ----
LOCAL_PATH := $(MY_JNI)
SRC_ROOT := $(MY_JNI)/../../../../../src
STB_ROOT := $(MY_JNI)/../../../../../thirdparty/stb

include $(CLEAR_VARS)
LOCAL_MODULE := siglus

LOCAL_CFLAGS := -DSIGLUS_ANDROID=1 -O2 -Wall \
    -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter
LOCAL_CPPFLAGS := -std=c++17 -frtti -fexceptions

LOCAL_C_INCLUDES := \
    $(SRC_ROOT) \
    $(SDL_ROOT)/include

LOCAL_SRC_FILES := \
    $(SRC_ROOT)/core/FileSystem.cpp \
    $(SRC_ROOT)/archive/Archive.cpp \
    $(SRC_ROOT)/media/Decoder.cpp \
    $(SRC_ROOT)/script/Script.cpp \
    $(SRC_ROOT)/gfx/Renderer.cpp \
    $(SRC_ROOT)/text/Text.cpp \
    $(SRC_ROOT)/audio/Audio.cpp \
    $(SRC_ROOT)/input/Input.cpp \
    $(SRC_ROOT)/app/Engine.cpp \
    $(SRC_ROOT)/app/main.cpp \
    $(SRC_ROOT)/platform/VideoPlayer_android.cpp

LOCAL_SHARED_LIBRARIES := SDL2
# SDL 2.30 的主程序静态库模块名是 SDL2_main（产物为 libSDL2main.a）
LOCAL_STATIC_LIBRARIES := SDL2_main
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lz

# stb 可选：存在才启用图像/字体/OGG 解码
ifneq ($(wildcard $(STB_ROOT)/stb_image.h),)
  LOCAL_C_INCLUDES += $(STB_ROOT)
  LOCAL_CPPFLAGS += -DSIGLUS_HAVE_STB=1 -DSIGLUS_HAVE_STB_IMAGE=1 -DSIGLUS_HAVE_STB_TRUETYPE=1 -DSIGLUS_HAVE_STB_VORBIS=1
else
  LOCAL_CPPFLAGS += -DSIGLUS_HAVE_STB=0
endif

include $(BUILD_SHARED_LIBRARY)
