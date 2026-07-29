#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <elf.h>
#include <link.h>

// Fallback 1: find libil2cpp via /proc/self/maps directly
void *find_libil2cpp_via_maps() {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return nullptr;
    
    char line[4096];
    void *base = nullptr;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so")) {
            unsigned long addr;
            sscanf(line, "%lx", &addr);
            base = (void *)addr;
            LOGI("Found libil2cpp via maps at %p", base);
            break;
        }
    }
    fclose(maps);
    return base;
}

// Fallback 2: dlopen with various paths
void *try_dlopen_libil2cpp() {
    const char *paths[] = {
        "libil2cpp.so",
        "libil2cpp.so.0",
        nullptr
    };
    
    for (int i = 0; paths[i]; i++) {
        void *h = dlopen(paths[i], RTLD_NOLOAD | RTLD_NOW);
        if (h) {
            LOGI("dlopen found libil2cpp via %s: %p", paths[i], h);
            return h;
        }
    }
    return nullptr;
}

// Fallback 3: scan /data/app for base address
void *find_base_via_proc_maps() {
    // Try all supported arch map paths
    const char *paths[] = {
        "/proc/self/maps",
        nullptr
    };
    
    void *base = nullptr;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return nullptr;
    
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
            unsigned long start;
            sscanf(line, "%lx", &start);
            base = (void *)start;
            LOGI("Found executable libil2cpp at %p", base);
            break;
        }
    }
    fclose(f);
    return base;
}

// Core init - try multiple methods to find and init libil2cpp
bool init_libil2cpp() {
    void *handle = nullptr;
    
    // Method 1: xdl_open (works when called from within the process)
    LOGI("Trying xdl_open...");
    handle = xdl_open("libil2cpp.so", 0);
    if (handle) {
        LOGI("xdl_open success: %p", handle);
        il2cpp_api_init(handle);
        return true;
    }
    
    // Method 2: dlopen with RTLD_NOLOAD
    LOGI("Trying dlopen...");
    handle = try_dlopen_libil2cpp();
    if (handle) {
        LOGI("dlopen success: %p", handle);
        il2cpp_api_init(handle);
        return true;
    }
    
    // Method 3: find by maps then dlopen
    LOGI("Trying maps-based find...");
    void *base = find_libil2cpp_via_maps();
    if (base) {
        // Try to get a handle using the base address
        // dlopen by path of the mapped library
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[4096];
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "libil2cpp.so")) {
                    char path[1024] = {0};
                    // Extract path from the end of the line
                    char *p = strstr(line, "/data/app/");
                    if (p) {
                        // Remove newline
                        char *nl = strchr(p, '\n');
                        if (nl) *nl = 0;
                        strncpy(path, p, sizeof(path) - 1);
                        handle = dlopen(path, RTLD_NOLOAD | RTLD_NOW);
                        if (handle) {
                            LOGI("Found lib by path: %s -> %p", path, handle);
                            il2cpp_api_init(handle);
                            fclose(maps);
                            return true;
                        }
                    }
                }
            }
            fclose(maps);
        }
    }
    
    return false;
}

void hack_start(const char *game_data_dir) {
    LOGI("hack_start waiting for libil2cpp...");
    
    for (int i = 0; i < 30; i++) {
        if (init_libil2cpp()) {
            LOGI("il2cpp_api_init done, dumping...");
            il2cpp_dump(game_data_dir);
            return;
        }
        LOGI("Attempt %d failed, retrying...", i + 1);
        sleep(1);
    }
    
    LOGI("Failed to find libil2cpp after 30 attempts");
}

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;
    void *(*loadLibrary)(const char *libpath, int flag);
    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);
    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;
    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    sleep(5);

    auto libart = dlopen("libart.so", RTLD_NOW);
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                              "JNI_GetCreatedJavaVMs");
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
    JavaVM *vms_buf[1];
    JavaVM *vms;
    jsize num_vms;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }

    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif
