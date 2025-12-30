LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := memory_tool

# Include Paths
OVERLAY_PATH := Overlay
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes/Android_draw
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes/Android_touch
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes/Android_shm
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(OVERLAY_PATH)/Includes/native_surface

# Source Files
FILE_LIST := $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/Android_draw/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/Android_shm/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/Android_touch/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/ImGui/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/ImGui/backends/*.cpp)
FILE_LIST += $(wildcard $(LOCAL_PATH)/$(OVERLAY_PATH)/native_surface/*.cpp)

# MemoryTool Sources
LOCAL_SRC_FILES := main.cpp MemoryTool.cpp $(FILE_LIST:$(LOCAL_PATH)/%=%)

# Compilation Flags
LOCAL_CFLAGS += -w -s -fvisibility=hidden -fpermissive -fexceptions
LOCAL_CPPFLAGS += -std=c++17 -fexceptions
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -lz

include $(BUILD_EXECUTABLE)
