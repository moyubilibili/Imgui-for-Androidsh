#ifndef A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include <android/native_window.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstddef>
#include <string>
#include <vector>
#include <cstdio>

// ============================================================
// 运行时符号解析器：尝试所有可能的命名空间变体
// ============================================================
//   Variant 0: 原始符号 (android::)
//   Variant 1: 只替换第一个 7android -> 7android3gui (仅类移到 gui)
//   Variant 2: 替换所有 7android -> 7android3gui (嵌套类型也移到 gui)

#define LOGD(fmt, ...) fprintf(stderr, "[D] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)

static void* resolve_symbol(void* handle, const char* original_sig) {
    // Variant 0: 原始
    void* sym = dlsym(handle, original_sig);
    if (sym) return sym;

    std::string sig(original_sig);

    // Variant 1: 只替换第一个 "7android"
    size_t pos = sig.find("7android");
    if (pos != std::string::npos) {
        std::string v1 = sig;
        v1.insert(pos + 8, "3gui");
        sym = dlsym(handle, v1.c_str());
        if (sym) { LOGD("Symbol found (v1): %s", v1.c_str()); return sym; }
    }

    // Variant 2: 替换所有 "7android"
    {
        std::string v2 = sig;
        size_t p = 0;
        while ((p = v2.find("7android", p)) != std::string::npos) {
            v2.insert(p + 8, "3gui");
            p += 12;
        }
        if (v2 != sig) {
            sym = dlsym(handle, v2.c_str());
            if (sym) { LOGD("Symbol found (v2-all): %s", v2.c_str()); return sym; }
        }
    }

    LOGW("Symbol NOT found (all variants): %s", original_sig);
    return nullptr;
}

// 转储 libgui 中所有可能的符号（调试用）
static void dump_libgui_symbols() {
#ifdef __LP64__
    void* handle = dlopen("/system/lib64/libgui.so", RTLD_LAZY);
#else
    void* handle = dlopen("/system/lib/libgui.so", RTLD_LAZY);
#endif
    if (!handle) { LOGE("Cannot dlopen libgui.so: %s", dlerror()); return; }

    struct SymPair { const char* ns; const char* sig; };
    const SymPair candidates[] = {
        {"android::", "_ZN7android21SurfaceComposerClientC2Ev"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClientC2Ev"},
        {"android::", "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
        {"android::", "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
        {"android::", "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient23getInternalDisplayTokenEv"},
        {"android::", "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient21getPhysicalDisplayIdsEv"},
        {"android::", "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE"},
        {"android::", "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE"},
        {"android::", "_ZNK7android14SurfaceControl10getSurfaceEv"},
        {"gui::",     "_ZNK7android3gui14SurfaceControl10getSurfaceEv"},
        {"android::", "_ZN7android21SurfaceComposerClient11TransactionC2Ev"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient11TransactionC2Ev"},
        {"android::", "_ZN7android21SurfaceComposerClient11Transaction5applyEbb"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient11Transaction5applyEbb"},
        {"android::", "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi"},
        {"android::", "_ZN7android21SurfaceComposerClient11Transaction8setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb"},
        {"gui::",     "_ZN7android3gui21SurfaceComposerClient11Transaction8setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb"},
        {"android::", "_ZN7android13LayerMetadataC2Ev"},
        {"gui::",     "_ZN7android3gui13LayerMetadataC2Ev"},
        {"android::", "_ZN7android7String8C2EPKc"},
        {"gui::",     "_ZN7android3gui7String8C2EPKc"},
        {"and::RefBase","_ZNK7android7RefBase9incStrongEPKv"},
        {"and::RefBase","_ZNK7android7RefBase9decStrongEPKv"},
    };

    int found = 0;
    for (auto& c : candidates) {
        void* s = dlsym(handle, c.sig);
        if (s) { fprintf(stderr, "[+] %s - %s\n", c.ns, c.sig); found++; }
    }
    fprintf(stderr, "[I] Symbol dump complete: %d found, %zu total checked\n", found, sizeof(candidates)/sizeof(candidates[0]));
    dlclose(handle);
}

// ============================================================
// Stub types — 只用于栈上分配内存
// ============================================================
namespace stub {
    struct String8 { char buf[128]; };
    struct LayerMetadata { char buf[256]; };
    struct SurfaceComposerClient { char buf[1024]; };
    struct Transaction { char buf[512]; };
    struct SurfaceControl { char buf[64]; };
    struct Surface { char buf[64]; };
    struct PhysicalDisplayId { uint64_t value; };
    struct DisplayInfo {
        uint32_t w{0}, h{0};
        float xdpi{0}, ydpi{0}, fps{0}, density{0};
        uint8_t orientation{0};
        bool secure{false};
        int64_t appVsyncOffset{0}, presentationDeadline{0};
        uint32_t viewportW{0}, viewportH{0};
    };
}

// ============================================================
// FunctionTable — 通过 dlsym 解析的 API 函数指针
// ============================================================
struct FunctionTable {
    size_t androidVersion = 13;

    // 这些函数指针使用 void* 来引用 C++ 对象，
    // 在运行时通过函数指针调用，调用约定符合 Itanium C++ ABI
    void (*SCC_Construct)(void* self) = nullptr;

    // CreateSurface(void* self, const String8& name, uint32_t w, uint32_t h,
    //   int32_t format, uint32_t flags, const sp<IBinder>& parent,
    //   const LayerMetadata& meta, uint32_t* outTransformHint) -> sp<SurfaceControl>
    // 返回类型 sp<SurfaceControl> 本质上是单个指针 (StrongPointer)
    // 在 Itanium ABI 中返回 struct 需要用隐藏参数，这里直接返回 void*
    void* (*SCC_CreateSurface)(void* self, void* name,
        uint32_t w, uint32_t h, int32_t format, uint32_t flags,
        void* parent, void* meta, uint32_t* hint) = nullptr;

    void* (*SCC_GetInternalDisplayToken)() = nullptr;

    // GetPhysicalDisplayIds() -> std::vector<PhysicalDisplayId>
    // 需要特殊处理 - 我们在栈上分配 vector 区域并调用函数
    // 这里存原始函数指针，调用时通过 wrapper
    void* (*SCC_GetPhysicalDisplayIds_raw)() = nullptr;
    void* (*SCC_GetPhysicalDisplayToken_raw)(uint64_t) = nullptr;
    int32_t (*SCC_GetDisplayInfo_raw)(void*, void*) = nullptr;

    void (*Txn_Construct)(void* self) = nullptr;
    void* (*Txn_SetLayer)(void* self, void* sc, int32_t z) = nullptr;
    void* (*Txn_SetTrustedOverlay)(void* self, void* sc, bool trusted) = nullptr;
    int32_t (*Txn_Apply)(void* self, bool sync, bool oneWay) = nullptr;

    void* (*SC_GetSurface)(void* self) = nullptr;
    void* (*SC_Validate)(void* self) = nullptr;
    void  (*SC_Disconnect)(void* self) = nullptr;

    void (*RefBase_IncStrong)(void* self, void* id) = nullptr;
    void (*RefBase_DecStrong)(void* self, void* id) = nullptr;
    void (*String8_Construct)(void* self, const char* data) = nullptr;
    void (*String8_Destroy)(void* self) = nullptr;
    void (*LayerMeta_Construct)(void* self) = nullptr;
    void (*LayerMeta_SetInt32)(void* self, uint32_t type, int32_t value) = nullptr;

    // 包装函数：获取物理显示 ID 列表
    // GetPhysicalDisplayIds 返回 std::vector，通过隐藏参数返回
    // 我们在栈上分配 3*sizeof(void*) 的空间作为 vector 存储区
    bool getPhysicalDisplayIds(uint64_t* outId, size_t* outCount) {
        if (!SCC_GetPhysicalDisplayIds_raw) return false;

        // 在栈上分配 vector<> 存储区域 (3 个指针 = begin, end, end_of_storage)
        char vecBuf[3 * sizeof(void*)] = {0};

        // 调用原始函数 - 由于返回类型不匹配，我们直接通过 asm/abi 技巧
        // 实际上更好的方法是：重新声明函数指针类型为正确返回类型的
        typedef void (*RealGetIds)(void* result);
        auto realFn = reinterpret_cast<RealGetIds>(SCC_GetPhysicalDisplayIds_raw);
        realFn((void*)vecBuf);

        // 从 vector 中读取数据
        // vector 布局: [pointer to begin, pointer to end, pointer to capacity]
        void** beginPtr = (void**)((void**)vecBuf);
        void** endPtr   = (void**)((void**)vecBuf + sizeof(void*));

        size_t count = (size_t)((char*)(*endPtr) - (char*)(*beginPtr)) / sizeof(stub::PhysicalDisplayId);

        if (count > 0 && beginPtr && *beginPtr) {
            stub::PhysicalDisplayId* ids = (stub::PhysicalDisplayId*)(*beginPtr);
            *outId = ids[0].value;
            *outCount = count;
            return true;
        }
        return false;
    }

    // 包装函数：获取物理显示令牌
    bool getPhysicalDisplayToken(uint64_t id, void** outToken) {
        if (!SCC_GetPhysicalDisplayToken_raw) return false;
        // sp<IBinder> 返回 - Itanium ABI 使用隐藏参数返回 struct
        typedef void (*RealGetToken)(void* result, uint64_t id);
        auto realFn = reinterpret_cast<RealGetToken>(SCC_GetPhysicalDisplayToken_raw);
        char spBuf[sizeof(void*)] = {0};
        realFn((void*)spBuf, id);
        void* ptr = *(void**)spBuf;
        if (ptr) { *outToken = ptr; return true; }
        return false;
    }

    // 包装函数：获取显示信息
    bool getDisplayInfo(void* token, stub::DisplayInfo* info) {
        if (!SCC_GetDisplayInfo_raw) return false;
        // GetDisplayInfo(const sp<IBinder>&, DisplayInfo*)
        // sp<IBinder> 作为 const 引用传递，本质上是指向指针的指针
        typedef int32_t (*RealGetInfo)(const void** tokenRef, void* info);
        auto realFn = reinterpret_cast<RealGetInfo>(SCC_GetDisplayInfo_raw);
        const void* tokenPtr = token;
        int32_t ret = realFn(&tokenPtr, (void*)info);
        return ret == 0;
    }

    bool Init() {
        std::string verStr(128, 0);
        verStr.resize(__system_property_get("ro.build.version.release", verStr.data()));
        if (!verStr.empty()) androidVersion = std::stoi(verStr);
        LOGI("Android version: %zu", androidVersion);

#ifdef __LP64__
        auto* libgui   = dlopen("/system/lib64/libgui.so", RTLD_LAZY | RTLD_LOCAL);
        auto* libutils = dlopen("/system/lib64/libutils.so", RTLD_LAZY | RTLD_LOCAL);
#else
        auto* libgui   = dlopen("/system/lib/libgui.so", RTLD_LAZY | RTLD_LOCAL);
        auto* libutils = dlopen("/system/lib/libutils.so", RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!libgui) { LOGE("Cannot open libgui.so: %s", dlerror()); return false; }

        // Dump 所有符号（调试）
        dump_libgui_symbols();

        #define RESOLVE(Member, Sig) do { \
            void* _s = resolve_symbol(libgui, Sig); \
            if (!_s && libutils) _s = resolve_symbol(libutils, Sig); \
            Member = reinterpret_cast<decltype(Member)>(_s); } while(0)

        RESOLVE(RefBase_IncStrong, "_ZNK7android7RefBase9incStrongEPKv");
        RESOLVE(RefBase_DecStrong, "_ZNK7android7RefBase9decStrongEPKv");
        RESOLVE(String8_Construct, "_ZN7android7String8C2EPKc");
        RESOLVE(String8_Destroy,   "_ZN7android7String8D2Ev");

        RESOLVE(SCC_Construct, "_ZN7android21SurfaceComposerClientC2Ev");
        // CreateSurface 的两种 flags 类型
        RESOLVE(SCC_CreateSurface, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
        if (!SCC_CreateSurface)
            RESOLVE(SCC_CreateSurface, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");

        RESOLVE(SCC_GetInternalDisplayToken,     "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
        RESOLVE(SCC_GetPhysicalDisplayIds_raw,   "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
        RESOLVE(SCC_GetPhysicalDisplayToken_raw, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
        RESOLVE(SCC_GetDisplayInfo_raw,          "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE");
        RESOLVE(Txn_Construct,                   "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
        RESOLVE(Txn_SetLayer,                    "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
        RESOLVE(Txn_SetTrustedOverlay,           "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
        RESOLVE(Txn_Apply,                       "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
        RESOLVE(SC_GetSurface,                   "_ZNK7android14SurfaceControl10getSurfaceEv");
        RESOLVE(SC_Validate,                     "_ZNK7android14SurfaceControl8validateEv");
        RESOLVE(SC_Disconnect,                   "_ZN7android14SurfaceControl10disconnectEv");
        RESOLVE(LayerMeta_Construct,             "_ZN7android13LayerMetadataC2Ev");
        RESOLVE(LayerMeta_SetInt32,              "_ZN7android13LayerMetadata8setInt32Eji");

        if (!LayerMeta_Construct)
            RESOLVE(LayerMeta_Construct,         "_ZN7android3gui13LayerMetadataC2Ev");
        if (!LayerMeta_SetInt32)
            RESOLVE(LayerMeta_SetInt32,          "_ZN7android3gui13LayerMetadata8setInt32Eji");

        LOGI("Symbols: Construct=%s CreateSurface=%s GetToken=%s GetPhysIds=%s GetDispInfo=%s Txn=%s SC_GetSurface=%s LayerMeta=%s S8=%s",
            SCC_Construct?"OK":"--",
            SCC_CreateSurface?"OK":"--",
            SCC_GetInternalDisplayToken?"OK":"--",
            SCC_GetPhysicalDisplayIds_raw?"OK":"--",
            SCC_GetDisplayInfo_raw?"OK":"--",
            Txn_Construct?"OK":"--",
            SC_GetSurface?"OK":"--",
            LayerMeta_Construct?"OK":"--",
            String8_Construct?"OK":"--");

        return true;
    }
};

#endif // A_NATIVE_WINDOW_CREATOR_H