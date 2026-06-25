#ifndef A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include <android/native_window.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstddef>
#include <string>
#include <vector>

// ============================================================
// 运行时符号解析器：尝试所有可能的命名空间变体
// ============================================================
//
// 在 AOSP 14+ 中，LayerMetadata 移到了 android::gui 命名空间
// 在 AOSP 16+ 中，SurfaceComposerClient 等也移到了 android::gui
// 本模块自动尝试所有可能的符号名变体
//
// 变体策略：
//   Variant 0: 原始符号 (android::)
//   Variant 1: 只替换第一个 7android -> 7android3gui (类移到 gui, 嵌套类型不变)
//   Variant 2: 替换所有 7android -> 7android3gui (全部移到 gui)

// 解析符号，尝试最多 3 个变体
static void* resolve_symbol(void* handle, const char* original_sig) {
    // Variant 0: 原始
    void* sym = dlsym(handle, original_sig);
    if (sym) return sym;

    std::string sig(original_sig);

    // Variant 1: 只替换第一个 "7android"
    size_t pos = sig.find("7android");
    if (pos != std::string::npos) {
        std::string v1 = sig;
        v1.insert(pos + 8, "3gui");  // 7android → 7android3gui
        sym = dlsym(handle, v1.c_str());
        if (sym) {
            __android_log_print(ANDROID_LOG_DEBUG, "ImGui",
                "[*] Symbol found (v1): %s", v1.c_str());
            return sym;
        }
    }

    // Variant 2: 替换所有 "7android"
    {
        std::string v2 = sig;
        size_t p = 0;
        while ((p = v2.find("7android", p)) != std::string::npos) {
            v2.insert(p + 8, "3gui");
            p += 12; // 跳过 7android3gui
        }
        if (v2 != sig) {
            sym = dlsym(handle, v2.c_str());
            if (sym) {
                __android_log_print(ANDROID_LOG_DEBUG, "ImGui",
                    "[*] Symbol found (v2-all): %s", v2.c_str());
                return sym;
            }
        }
    }

    // 都没找到，记录一下
    __android_log_print(ANDROID_LOG_WARN, "ImGui",
        "[-] Symbol NOT found (all variants): %s", original_sig);
    return nullptr;
}

// 用于在 main.cpp 中打印实际符号名的调试函数
static void dump_libgui_symbols() {
#ifdef __LP64__
    void* handle = dlopen("/system/lib64/libgui.so", RTLD_LAZY);
#else
    void* handle = dlopen("/system/lib/libgui.so", RTLD_LAZY);
#endif
    if (!handle) {
        __android_log_print(ANDROID_LOG_ERROR, "ImGui",
            "Cannot dlopen libgui.so: %s", dlerror());
        return;
    }

    // 尝试一些已知的 SurfaceFlinger 符号，看实际哪个能找到
    const char* candidates[] = {
        // SurfaceComposerClient 构造函数
        "_ZN7android21SurfaceComposerClientC2Ev",
        "_ZN7android3gui21SurfaceComposerClientC2Ev",
        // CreateSurface 的多种变体
        "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
        "_ZN7android3gui21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
        "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
        "_ZN7android3gui21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj",
        // GetInternalDisplayToken
        "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv",
        "_ZN7android3gui21SurfaceComposerClient23getInternalDisplayTokenEv",
        // GetPhysicalDisplayIds
        "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv",
        "_ZN7android3gui21SurfaceComposerClient21getPhysicalDisplayIdsEv",
        // GetPhysicalDisplayToken
        "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE",
        "_ZN7android3gui21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE",
        // GetDisplayInfo
        "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE",
        "_ZN7android3gui21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE",
        // SurfaceControl::GetSurface
        "_ZNK7android14SurfaceControl10getSurfaceEv",
        "_ZNK7android3gui14SurfaceControl10getSurfaceEv",
        // SurfaceComposerClient::Transaction
        "_ZN7android21SurfaceComposerClient11TransactionC2Ev",
        "_ZN7android3gui21SurfaceComposerClient11TransactionC2Ev",
        // Transaction::Apply
        "_ZN7android21SurfaceComposerClient11Transaction5applyEbb",
        "_ZN7android3gui21SurfaceComposerClient11Transaction5applyEbb",
        // Transaction::SetLayer
        "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi",
        "_ZN7android3gui21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi",
        // SetTrustedOverlay
        "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb",
        "_ZN7android3gui21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb",
        // LayerMetadata
        "_ZN7android13LayerMetadataC2Ev",
        "_ZN7android3gui13LayerMetadataC2Ev",
        // String8
        "_ZN7android7String8C2EPKc",
        "_ZN7android3gui7String8C2EPKc",
        // RefBase
        "_ZNK7android7RefBase9incStrongEPKv",
        "_ZNK7android7RefBase9decStrongEPKv",
    };

    for (auto sig : candidates) {
        void* s = dlsym(handle, sig);
        if (s) {
            __android_log_print(ANDROID_LOG_DEBUG, "ImGui",
                "[+] FOUND: %s", sig);
        }
    }
    dlclose(handle);
}

// ============================================================
// Stub types for stack-based construction
// ============================================================
namespace stub {
    struct String8 {
        char buf[128];
    };
    struct LayerMetadata {
        char buf[256];
    };
    template <typename T>
    struct StrongPointer {
        T* ptr = nullptr;
        T* get() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }
    };
    struct SurfaceControl {
        char buf[64];
    };
    struct Surface {
        char buf[64];
    };
    struct SurfaceComposerClient {
        char buf[512];
    };
    struct Transaction {
        char buf[512];
    };
    struct DisplayInfo {
        uint32_t w{0};
        uint32_t h{0};
        float xdpi{0};
        float ydpi{0};
        float fps{0};
        float density{0};
        uint8_t orientation{0};
        bool secure{false};
        int64_t appVsyncOffset{0};
        int64_t presentationDeadline{0};
        uint32_t viewportW{0};
        uint32_t viewportH{0};
    };
    struct DisplayState {
        char buf[64];
    };
    struct PhysicalDisplayId {
        uint64_t value;
    };
}

// ============================================================
// Function table — 通过 dlsym 解析的 API 函数指针
// ============================================================
struct FunctionTable {
    size_t androidVersion = 13;

    // SurfaceComposerClient
    void (*SCC_Construct)(void* self) = nullptr;
    void (*SCC_Destroy)(void* self) = nullptr;
    void* (*SCC_CreateSurface)(void* self, void* name,
        uint32_t w, uint32_t h, int32_t format, uint32_t flags,
        void* parentHandle, void* layerMetadata, uint32_t* outTransformHint) = nullptr;
    void* (*SCC_GetInternalDisplayToken)() = nullptr;
    void* (*SCC_GetPhysicalDisplayIds)() = nullptr;
    void* (*SCC_GetPhysicalDisplayToken)(uint64_t displayId) = nullptr;
    int32_t (*SCC_GetDisplayInfo)(void* token, void* info) = nullptr;

    // Transaction
    void (*Txn_Construct)(void* self) = nullptr;
    void* (*Txn_SetLayer)(void* self, void* surfaceControl, int32_t z) = nullptr;
    void* (*Txn_SetTrustedOverlay)(void* self, void* surfaceControl, bool trusted) = nullptr;
    int32_t (*Txn_Apply)(void* self, bool sync, bool oneWay) = nullptr;

    // SurfaceControl
    void* (*SC_GetSurface)(void* self) = nullptr;
    int32_t (*SC_Validate)(void* self) = nullptr;
    void (*SC_Disconnect)(void* self) = nullptr;

    // RefBase (libutils)
    void (*RefBase_IncStrong)(void* self, void* id) = nullptr;
    void (*RefBase_DecStrong)(void* self, void* id) = nullptr;

    // String8 (libutils)
    void (*String8_Construct)(void* self, const char* data) = nullptr;
    void (*String8_Destroy)(void* self) = nullptr;

    // LayerMetadata
    void (*LayerMeta_Construct)(void* self) = nullptr;
    void (*LayerMeta_SetInt32)(void* self, uint32_t type, int32_t value) = nullptr;

    bool Init() {
        std::string verStr(128, 0);
        verStr.resize(__system_property_get("ro.build.version.release", verStr.data()));
        if (!verStr.empty()) androidVersion = std::stoi(verStr);

        __android_log_print(ANDROID_LOG_INFO, "ImGui",
            "Android version: %zu", androidVersion);

#ifdef __LP64__
        auto* libgui   = dlopen("/system/lib64/libgui.so", RTLD_LAZY | RTLD_LOCAL);
        auto* libutils = dlopen("/system/lib64/libutils.so", RTLD_LAZY | RTLD_LOCAL);
#else
        auto* libgui   = dlopen("/system/lib/libgui.so", RTLD_LAZY | RTLD_LOCAL);
        auto* libutils = dlopen("/system/lib/libutils.so", RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!libgui) {
            __android_log_print(ANDROID_LOG_ERROR, "ImGui",
                "Cannot open libgui.so: %s", dlerror());
            return false;
        }

        // 先 dump 所有已知符号（调试）
        dump_libgui_symbols();

        // 逐个解析 — 每个符号尝试所有命名空间变体
        #define RESOLVE(Member, Sig) \
            do { \
                void* _s = resolve_symbol(libgui, Sig); \
                if (!_s && libutils) _s = resolve_symbol(libutils, Sig); \
                Member = reinterpret_cast<decltype(Member)>(_s); \
            } while(0)

        // libutils
        RESOLVE(RefBase_IncStrong,  "_ZNK7android7RefBase9incStrongEPKv");
        RESOLVE(RefBase_DecStrong,  "_ZNK7android7RefBase9decStrongEPKv");
        RESOLVE(String8_Construct,  "_ZN7android7String8C2EPKc");
        RESOLVE(String8_Destroy,    "_ZN7android7String8D2Ev");

        if (!String8_Construct) {
            // 可能 String8 在 android:: 下，尝试 libcutils 或直接 hardcode
            __android_log_print(ANDROID_LOG_ERROR, "ImGui",
                "String8 constructor NOT resolved! Trying gui namespace...");
        }

        // libgui
        RESOLVE(SCC_Construct,           "_ZN7android21SurfaceComposerClientC2Ev");
        RESOLVE(SCC_CreateSurface,       "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
        // 也尝试用 int 做 flags 参数的旧版本
        if (!SCC_CreateSurface) {
            RESOLVE(SCC_CreateSurface,   "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
        }
        RESOLVE(SCC_GetInternalDisplayToken, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
        RESOLVE(SCC_GetPhysicalDisplayIds,   "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
        RESOLVE(SCC_GetPhysicalDisplayToken, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
        RESOLVE(SCC_GetDisplayInfo,          "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE");
        RESOLVE(Txn_Construct,               "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
        RESOLVE(Txn_SetLayer,                "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
        RESOLVE(Txn_SetTrustedOverlay,       "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
        RESOLVE(Txn_Apply,                   "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
        RESOLVE(SC_GetSurface,               "_ZNK7android14SurfaceControl10getSurfaceEv");
        RESOLVE(SC_Validate,                 "_ZNK7android14SurfaceControl8validateEv");
        RESOLVE(SC_Disconnect,               "_ZN7android14SurfaceControl10disconnectEv");
        RESOLVE(LayerMeta_Construct,         "_ZN7android13LayerMetadataC2Ev");
        RESOLVE(LayerMeta_SetInt32,          "_ZN7android13LayerMetadata8setInt32Eji");

        // 对于 Android 14+，LayerMetadata 可能在 android::gui:: 下
        if (!LayerMeta_Construct) {
            RESOLVE(LayerMeta_Construct,     "_ZN7android3gui13LayerMetadataC2Ev");
        }
        if (!LayerMeta_SetInt32) {
            RESOLVE(LayerMeta_SetInt32,      "_ZN7android3gui13LayerMetadata8setInt32Eji");
        }

        // 报告解析结果
        __android_log_print(ANDROID_LOG_INFO, "ImGui",
            "Symbol resolution: SCC_Construct=%s, SCC_CreateSurface=%s, "
            "GetInternalDisplayToken=%s, GetPhysicalDisplayIds=%s, "
            "GetDisplayInfo=%s, Txn=%s, SC_GetSurface=%s, LayerMeta=%s, "
            "String8=%s",
            SCC_Construct ? "OK" : "FAIL",
            SCC_CreateSurface ? "OK" : "FAIL",
            SCC_GetInternalDisplayToken ? "OK" : "FAIL",
            SCC_GetPhysicalDisplayIds ? "OK" : "FAIL",
            SCC_GetDisplayInfo ? "OK" : "FAIL",
            Txn_Construct ? "OK" : "FAIL",
            SC_GetSurface ? "OK" : "FAIL",
            LayerMeta_Construct ? "OK" : "FAIL",
            String8_Construct ? "OK" : "FAIL");

        return true;
    }
};

#endif // A_NATIVE_WINDOW_CREATOR_H