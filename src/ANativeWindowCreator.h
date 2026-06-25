#ifndef A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include <android/native_window.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstddef>
#include <unordered_map>
#include <string>
#include <vector>

// 尝试两种命名空间解析符号：先在 android:: 下找，找不到则在 android::gui:: 下找
#define ResolveMethodDual(ClassName, MethodName, Handle, AndroidSig)                   \
    do {                                                                               \
        ClassName##__##MethodName = reinterpret_cast<decltype(ClassName##__##MethodName)>( \
            symbolMethod.Find(Handle, AndroidSig));                                    \
        if (nullptr == ClassName##__##MethodName) {                                    \
            /* 尝试 android::gui:: 命名空间 (AOSP 16+) */                              \
            std::string guiSig = AndroidSig;                                           \
            /* "7android" + 类名长度数字 => "7android3gui" + 类名长度数字 */           \
            size_t pos = guiSig.find("7android");                                      \
            if (pos != std::string::npos) {                                            \
                guiSig.insert(pos + 8, "3gui");                                        \
                ClassName##__##MethodName = reinterpret_cast<decltype(ClassName##__##MethodName)>( \
                    symbolMethod.Find(Handle, guiSig.c_str()));                        \
            }                                                                          \
        }                                                                              \
        if (nullptr == ClassName##__##MethodName) {                                    \
            __android_log_print(ANDROID_LOG_ERROR, "ImGui",                            \
                "[-] Method not found (both namespaces): %s::%s",                      \
                #ClassName, #MethodName);                                              \
        }                                                                              \
    } while(0)

namespace android {
    namespace detail {
        namespace ui {
            struct LayerStack {
                uint32_t id = UINT32_MAX;
            };
            enum class Rotation {
                Rotation0 = 0,
                Rotation90 = 1,
                Rotation180 = 2,
                Rotation270 = 3
            };
            struct Size {
                int32_t width = -1;
                int32_t height = -1;
            };
            struct DisplayState {
                LayerStack layerStack;
                Rotation orientation = Rotation::Rotation0;
                Size layerStackSpaceRect;
            };
            typedef int64_t nsecs_t;
            struct DisplayInfo {
                uint32_t w{0};
                uint32_t h{0};
                float xdpi{0};
                float ydpi{0};
                float fps{0};
                float density{0};
                uint8_t orientation{0};
                bool secure{false};
                nsecs_t appVsyncOffset{0};
                nsecs_t presentationDeadline{0};
                uint32_t viewportW{0};
                uint32_t viewportH{0};
            };
            enum class DisplayType {
                DisplayIdMain = 0,
                DisplayIdHdmi = 1
            };
            struct PhysicalDisplayId {
                uint64_t value;
            };
        }
        struct String8;
        struct LayerMetadata;
        struct Surface;
        struct SurfaceControl;
        struct SurfaceComposerClientTransaction;
        struct SurfaceComposerClient;
        template <typename any_t>
        struct StrongPointer {
            union {
                any_t* pointer;
                char padding[sizeof(std::max_align_t)];
            };
            inline any_t* operator->() const { return pointer; }
            inline any_t* get() const { return pointer; }
            inline explicit operator bool() const { return nullptr != pointer; }
        };
        struct Functionals {
            struct SymbolMethod {
                void* (*Open)(const char* filename, int flag) = nullptr;
                void* (*Find)(void* handle, const char* symbol) = nullptr;
                int   (*Close)(void* handle) = nullptr;
            };
            size_t systemVersion = 13;

            void (*RefBase__IncStrong)(void* thiz, void* id) = nullptr;
            void (*RefBase__DecStrong)(void* thiz, void* id) = nullptr;

            void (*String8__Constructor)(void* thiz, const char* const data) = nullptr;
            void (*String8__Destructor)(void* thiz) = nullptr;

            void (*LayerMetadata__Constructor)(void* thiz) = nullptr;
            void (*LayerMetadata__setInt32)(void* thiz, uint32_t type, int32_t value) = nullptr;

            void (*SurfaceComposerClient__Constructor)(void* thiz) = nullptr;
            void (*SurfaceComposerClient__Destructor)(void* thiz) = nullptr;

            StrongPointer<void>
            (*SurfaceComposerClient__CreateSurface)(void* thiz, void* name, uint32_t w, uint32_t h,
                                                     int32_t format, uint32_t flags, void* parentHandle,
                                                     void* layerMetadata, uint32_t* outTransformHint) = nullptr;

            StrongPointer<void> (*SurfaceComposerClient__GetInternalDisplayToken)() = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetBuiltInDisplay)(ui::DisplayType type) = nullptr;

            int32_t (*SurfaceComposerClient__GetDisplayState)(StrongPointer<void>& display,
                                                               ui::DisplayState* displayState) = nullptr;
            int32_t (*SurfaceComposerClient__GetDisplayInfo)(StrongPointer<void>& display,
                                                              ui::DisplayInfo* displayInfo) = nullptr;

            std::vector<ui::PhysicalDisplayId> (*SurfaceComposerClient__GetPhysicalDisplayIds)() = nullptr;
            StrongPointer<void>
            (*SurfaceComposerClient__GetPhysicalDisplayToken)(ui::PhysicalDisplayId displayId) = nullptr;

            void (*SurfaceComposerClient__Transaction__Constructor)(void* thiz) = nullptr;
            void* (*SurfaceComposerClient__Transaction__SetLayer)(void* thiz, StrongPointer<void>& surfaceControl,
                                                                   int32_t z) = nullptr;
            void* (*SurfaceComposerClient__Transaction__SetTrustedOverlay)(void* thiz,
                                                                           StrongPointer<void>& surfaceControl,
                                                                           bool isTrustedOverlay) = nullptr;
            int32_t (*SurfaceComposerClient__Transaction__Apply)(void* thiz, bool synchronous, bool oneWay) = nullptr;

            int32_t (*SurfaceControl__Validate)(void* thiz) = nullptr;
            StrongPointer<Surface> (*SurfaceControl__GetSurface)(void* thiz) = nullptr;
            void (*SurfaceControl__DisConnect)(void* thiz) = nullptr;

            Functionals(const SymbolMethod& symbolMethod) {
                std::string systemVersionString(128, 0);
                systemVersionString.resize(
                    __system_property_get("ro.build.version.release", systemVersionString.data()));
                if (!systemVersionString.empty())
                    systemVersion = std::stoi(systemVersionString);
                if (systemVersion < 8) {
                    __android_log_print(ANDROID_LOG_ERROR, "ImGui", "[-] Unsupported system version: %zu", systemVersion);
                    return;
                }

#ifdef __LP64__
                auto libgui   = symbolMethod.Open("/system/lib64/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib64/libutils.so", RTLD_LAZY);
#else
                auto libgui   = symbolMethod.Open("/system/lib/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib/libutils.so", RTLD_LAZY);
#endif
                // libutils (RefBase + String8) — 命名空间不变
                ResolveMethodDual(RefBase, IncStrong, libutils, "_ZNK7android7RefBase9incStrongEPKv");
                ResolveMethodDual(RefBase, DecStrong, libutils, "_ZNK7android7RefBase9decStrongEPKv");
                ResolveMethodDual(String8, Constructor, libutils, "_ZN7android7String8C2EPKc");
                ResolveMethodDual(String8, Destructor, libutils, "_ZN7android7String8D2Ev");

                // libgui SurfaceComposerClient 方法 (自动尝试 android:: 和 android::gui::)
                ResolveMethodDual(SurfaceComposerClient, Constructor, libgui, "_ZN7android21SurfaceComposerClientC2Ev");
                ResolveMethodDual(LayerMetadata, Constructor, libgui, "_ZN7android13LayerMetadataC2Ev");
                ResolveMethodDual(LayerMetadata, setInt32, libgui, "_ZN7android13LayerMetadata8setInt32Eji");
                ResolveMethodDual(SurfaceComposerClient, CreateSurface, libgui,
                    "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj");
                ResolveMethodDual(SurfaceComposerClient, GetInternalDisplayToken, libgui,
                    "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
                ResolveMethodDual(SurfaceComposerClient, GetDisplayState, libgui,
                    "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
                ResolveMethodDual(SurfaceComposerClient, GetDisplayInfo, libgui,
                    "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_2ui11DisplayInfoE");
                ResolveMethodDual(SurfaceComposerClient, GetPhysicalDisplayIds, libgui,
                    "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
                ResolveMethodDual(SurfaceComposerClient, GetPhysicalDisplayToken, libgui,
                    "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
                ResolveMethodDual(SurfaceComposerClient__Transaction, Constructor, libgui,
                    "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
                ResolveMethodDual(SurfaceComposerClient__Transaction, SetLayer, libgui,
                    "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
                ResolveMethodDual(SurfaceComposerClient__Transaction, SetTrustedOverlay, libgui,
                    "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
                ResolveMethodDual(SurfaceComposerClient__Transaction, Apply, libgui,
                    "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
                ResolveMethodDual(SurfaceControl, Validate, libgui, "_ZNK7android14SurfaceControl8validateEv");
                ResolveMethodDual(SurfaceControl, GetSurface, libgui, "_ZN7android14SurfaceControl10getSurfaceEv");
                ResolveMethodDual(SurfaceControl, DisConnect, libgui, "_ZN7android14SurfaceControl10disconnectEv");

                // 尝试 patchesTable (旧版本兼容)
                static std::unordered_map<size_t, std::unordered_map<void**, const char*>> patchesTable = {
                    {15, {
                        {reinterpret_cast<void**>(&LayerMetadata__Constructor),
                         "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void**>(&SurfaceComposerClient__CreateSurface),
                         "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                        {reinterpret_cast<void**>(&LayerMetadata__setInt32),
                         "_ZN7android3gui13LayerMetadata8setInt32Eji"},
                    }},
                    {14, {
                        {reinterpret_cast<void**>(&LayerMetadata__Constructor),
                         "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void**>(&SurfaceComposerClient__CreateSurface),
                         "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                    }},
                };
                if (SurfaceComposerClient__CreateSurface == nullptr && patchesTable.find(systemVersion) != patchesTable.end()) {
                    for (const auto& [patchTo, signature] : patchesTable.at(systemVersion)) {
                        *patchTo = symbolMethod.Find(libgui, signature);
                    }
                }
            }
        };
    }
}

#endif // A_NATIVE_WINDOW_CREATOR_H