//
// Created by fgsqme on 2022/9/9.
//

#include "extern_function.h"
#include "native_surface/aosp/native_surface_9.h"
#include "native_surface/aosp/native_surface_10.h"
#include "native_surface/aosp/native_surface_11.h"
#include "native_surface/aosp/native_surface_12.h"
#include "native_surface/aosp/native_surface_12_1.h"
#include "native_surface/aosp/native_surface_13.h"
#include "native_surface/aosp/dev.h"

// 动态库方案
static void *handle;
static FuncPointer funcPointer;

ExternFunction::ExternFunction() {
    int api_level = get_android_api_level();
    printf("Android API Level: %d\n", api_level);

    // Common setup for recent Android versions
    if (api_level >= 30) {
        exec_native_surface("settings put global block_untrusted_touches 0");
    }

    if (!handle) {
        if (api_level >= 33) { // Android 13, 14, 15... (Try capabilities of 13)
#ifdef __aarch64__
            printf("Loading Native Surface for Android 13+ (64-bit)\n");
            handle = dlblob(&native_surface_13_64, sizeof(native_surface_13_64));
#else
            printf("Loading Native Surface for Android 13+ (32-bit)\n");
            handle = dlblob(&native_surface_13_32, sizeof(native_surface_13_32));
#endif
        } else if (api_level == 32) { // Android 12.1
#ifdef __aarch64__
            handle = dlblob(&native_surface_12_1_64, sizeof(native_surface_12_1_64));
#else
            handle = dlblob(&native_surface_12_1_32, sizeof(native_surface_12_1_32));
#endif
        } else if (api_level == 31) { // Android 12
#ifdef __aarch64__
            handle = dlblob(&native_surface_12_64, sizeof(native_surface_12_64));
#else
            handle = dlblob(&native_surface_12_32, sizeof(native_surface_12_32));
#endif
        } else if (api_level == 30) { // Android 11
#ifdef __aarch64__
            handle = dlblob(&native_surface_11_64, sizeof(native_surface_11_64));
#else
            handle = dlblob(&native_surface_11_32, sizeof(native_surface_11_32));
#endif
        } else if (api_level == 29) { // Android 10
#ifdef __aarch64__
            handle = dlblob(&native_surface_10_64, sizeof(native_surface_10_64));
#else
            handle = dlblob(&native_surface_10_32, sizeof(native_surface_10_32));
#endif
        } else if (api_level == 28) { // Android 9
#ifdef __aarch64__
            handle = dlblob(&native_surface_9_64, sizeof(native_surface_9_64));
#else
            handle = dlblob(&native_surface_9_32, sizeof(native_surface_9_32));
#endif
        } else {
            printf("Error: Unsupported Android API Level: %d\n", api_level);
            exit(0);
        }

        if (!handle) {
            printf("Error: Failed to load native_surface blob!\n");
            exit(0);
        }

        funcPointer.func_createNativeWindow = dlsym(handle, "_Z18createNativeWindowPKcjjb");
        funcPointer.func_getDisplayInfo = dlsym(handle, "_Z14getDisplayInfov");
        
        // Android 12+ usually has "more parameters" version
        if (api_level >= 31) {
             funcPointer.func_more_createNativeWindow = dlsym(handle, "_Z18createNativeWindowPKcjjjjb");
        }
    }

}

/**
 * 创建 native surface
 * @param surface_name 创建名称
 * @param screen_width 创建宽度
 * @param screen_height 创建高度
 * @param author 是否打印作者信息
 * @return
 */
ANativeWindow *
ExternFunction::createNativeWindow(const char *surface_name, uint32_t screen_width, uint32_t screen_height,
                                   bool author) {
    return ((ANativeWindow *(*)(
            const char *, uint32_t, uint32_t, bool))
            (funcPointer.func_createNativeWindow))(surface_name, screen_width, screen_height, author);
}

/**
 * (更多可选参数)创建 native surface
 * @param surface_name 创建名称
 * @param screen_width 创建宽度
 * @param screen_height 创建高度
 * @param format format
 * @param flags flags
 * @param author 是否打印作者信息
 * @return
 */
ANativeWindow *
ExternFunction::createNativeWindow(const char *surface_name, uint32_t screen_width, uint32_t screen_height,
                                   uint32_t format, uint32_t flags, bool author) {
    return ((ANativeWindow *(*)(
            const char *, uint32_t, uint32_t, uint32_t, uint32_t, bool))
            (funcPointer.func_more_createNativeWindow))(surface_name, screen_width, screen_height, format, flags,
                                                        author);
}

/**
 * 获取屏幕宽高以及旋转状态
 */
MDisplayInfo ExternFunction::getDisplayInfo() {
    return ((MDisplayInfo (*)()) (funcPointer.func_getDisplayInfo))();
}
