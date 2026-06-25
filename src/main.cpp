#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <cstring>
#include <new>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "ANativeWindowCreator.h"

#define LOGI(fmt, ...) fprintf(stdout, "[ImGui] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ImGui] " fmt "\n", ##__VA_ARGS__)

// ============================================================
// EGL / 窗口全局变量
// ============================================================
static EGLDisplay  egl_disp  = EGL_NO_DISPLAY;
static EGLContext  egl_ctx   = EGL_NO_CONTEXT;
static EGLSurface  egl_surf  = EGL_NO_SURFACE;
static ANativeWindow* g_window = nullptr;
static uint32_t fb_width  = 0;
static uint32_t fb_height = 0;

// ============================================================
// SurfaceFlinger -> ANativeWindow 创建（使用 FunctionTable）
// ============================================================
static bool create_overlay_window(FunctionTable& fn) {
    // --- 1. 在栈上构造 SurfaceComposerClient ---
    char clientBuf[1024] = {0};
    void* scc = (void*)clientBuf;

    if (!fn.SCC_Construct) {
        LOGE("SCC_Construct not resolved");
        return false;
    }
    fn.SCC_Construct(scc);
    LOGI("SurfaceComposerClient constructed");

    // --- 2. 获取显示 Token ---
    bool haveToken = false;
    char tokenBuf[64] = {0};

    auto getToken = [&](const char* method) -> bool {
        // 这里需要一个合适大小的结构来接收 IBinder
        // 实际上 GetInternalDisplayToken 返回的是 sp<IBinder>
        // 但在栈上构造太复杂，改用指针
        return false;
    };

    // 使用 getPhysicalDisplayIds + getPhysicalDisplayToken
    if (fn.SCC_GetPhysicalDisplayIds && fn.SCC_GetPhysicalDisplayToken) {
        // 注意：GetPhysicalDisplayIds 返回 vector<PhysicalDisplayId>
        // 我们需要调用它并获取返回的数组
        // 这里使用 reinterpret_cast 技巧：调用返回 vector 的函数
        typedef std::vector<stub::PhysicalDisplayId> (*GetIdsFunc)();
        auto getIds = reinterpret_cast<GetIdsFunc>(fn.SCC_GetPhysicalDisplayIds);
        auto ids = getIds();

        if (!ids.empty()) {
            LOGI("Found %zu physical display(s)", ids.size());

            stub::PhysicalDisplayId id0 = ids[0];
            // getPhysicalDisplayToken(uint64_t) -> sp<IBinder>
            typedef stub::StrongPointer<void> (*GetTokenFunc)(uint64_t);
            auto getToken = reinterpret_cast<GetTokenFunc>(fn.SCC_GetPhysicalDisplayToken);
            auto tokenSP = getToken(id0.value);

            if (tokenSP) {
                LOGI("Got display token via getPhysicalDisplayToken");
                haveToken = true;

                // --- 3. 获取显示信息 ---
                stub::DisplayInfo dinfo = {0};
                if (fn.SCC_GetDisplayInfo) {
                    // GetDisplayInfo(const sp<IBinder>&, DisplayInfo*)
                    // 第一个参数是 const sp<IBinder>&，即 const StrongPointer<IBinder>&
                    typedef int32_t (*GetInfoFunc)(void* /* const sp<IBinder>& */, void*);
                    auto getInfo = reinterpret_cast<GetInfoFunc>(fn.SCC_GetDisplayInfo);
                    int32_t ret = getInfo((void*)&tokenSP, (void*)&dinfo);
                    LOGI("GetDisplayInfo returned %d: %ux%u", ret, dinfo.w, dinfo.h);
                } else {
                    LOGE("SCC_GetDisplayInfo not resolved");
                }

                fb_width  = dinfo.w > 0 ? dinfo.w : 1080;
                fb_height = dinfo.h > 0 ? dinfo.h : 1920;
                LOGI("Display resolution: %dx%d", fb_width, fb_height);
            }
        } else {
            LOGE("getPhysicalDisplayIds returned empty list");
        }
    } else {
        LOGE("GetPhysicalDisplayIds/Token not resolved");
    }

    if (!haveToken) {
        // 尝试 getInternalDisplayToken
        if (fn.SCC_GetInternalDisplayToken) {
            typedef stub::StrongPointer<void> (*TokenFunc)();
            auto getToken = reinterpret_cast<TokenFunc>(fn.SCC_GetInternalDisplayToken);
            auto token = getToken();
            if (token) {
                LOGI("Got display token via getInternalDisplayToken");
                haveToken = true;

                stub::DisplayInfo dinfo = {0};
                if (fn.SCC_GetDisplayInfo) {
                    typedef int32_t (*GetInfoFunc)(void*, void*);
                    auto getInfo = reinterpret_cast<GetInfoFunc>(fn.SCC_GetDisplayInfo);
                    getInfo((void*)&token, (void*)&dinfo);
                }
                fb_width  = dinfo.w > 0 ? dinfo.w : 1080;
                fb_height = dinfo.h > 0 ? dinfo.h : 1920;
            }
        }
    }

    if (!haveToken) {
        LOGE("Failed to get display token via any method");
        return false;
    }

    // --- 4. 创建 String8 ---
    char str8Buf[128] = {0};
    stub::String8* str8 = (stub::String8*)str8Buf;
    if (fn.String8_Construct)
        fn.String8_Construct(str8, "ImGuiOverlay");

    // --- 5. LayerMetadata ---
    char metaBuf[256] = {0};
    if (fn.LayerMeta_Construct)
        fn.LayerMeta_Construct(metaBuf);
    if (fn.LayerMeta_SetInt32)
        fn.LayerMeta_SetInt32(metaBuf, 0, 1);

    // --- 6. 创建 Surface ---
    if (!fn.SCC_CreateSurface) {
        LOGE("SCC_CreateSurface not resolved");
        return false;
    }

    uint32_t transformHint = 0;
    // CreateSurface 返回 sp<SurfaceControl>
    typedef void* (*CreateSurfaceFunc)(void* self, void* name,
        uint32_t w, uint32_t h, int32_t format, uint32_t flags,
        void* parent, void* meta, uint32_t* hint);
    auto createSurf = reinterpret_cast<CreateSurfaceFunc>(fn.SCC_CreateSurface);
    void* scSP = createSurf(scc, str8, fb_width, fb_height,
        1,  // PIXEL_FORMAT_RGBA_8888
        1,  // ISurfaceComposerClient::eFXSurfaceBufferState
        nullptr, metaBuf, &transformHint);

    if (!scSP) {
        LOGE("createSurface returned null");
        return false;
    }
    LOGI("SurfaceControl created");

    // --- 7. 设置 Transaction ---
    struct SurfaceControlDeleter {
        stub::StrongPointer<void> sp;
        char padding[192 - sizeof(stub::StrongPointer<void>)] = {0};
    };
    // scSP 是一个 StrongPointer<SurfaceControl>，它的地址就是 SurfaceControl 的地址
    // 但是我们无法确定 StrongPointer 的布局，所以直接用原始指针

    char txnBuf[512] = {0};
    if (fn.Txn_Construct)
        fn.Txn_Construct(txnBuf);

    if (fn.Txn_SetLayer)
        fn.Txn_SetLayer(txnBuf, scSP, INT32_MAX);
    if (fn.Txn_SetTrustedOverlay)
        fn.Txn_SetTrustedOverlay(txnBuf, scSP, true);
    if (fn.Txn_Apply)
        fn.Txn_Apply(txnBuf, true, true);

    // --- 8. 获取 ANativeWindow ---
    if (!fn.SC_GetSurface) {
        LOGE("SC_GetSurface not resolved");
        return false;
    }
    typedef void* (*GetSurfFunc)(void*);
    auto getSf = reinterpret_cast<GetSurfFunc>(fn.SC_GetSurface);
    void* surface = getSf(scSP);

    if (!surface) {
        LOGE("SC_GetSurface returned null");
        return false;
    }

    g_window = reinterpret_cast<ANativeWindow*>(surface);
    ANativeWindow_acquire(g_window);
    LOGI("ANativeWindow: %p (%dx%d)", (void*)g_window, fb_width, fb_height);
    return true;
}

// ============================================================
// EGL 初始化 (使用 ANativeWindow)
// ============================================================
static bool init_egl_with_window() {
    EGLConfig  config;
    EGLint     num_configs;
    EGLint     major, minor;

    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed: 0x%x", eglGetError());
        return false;
    }

    if (!eglInitialize(egl_disp, &major, &minor)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }
    LOGI("EGL version: %d.%d", major, minor);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,       8,
        EGL_GREEN_SIZE,      8,
        EGL_RED_SIZE,        8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE
    };

    if (!eglChooseConfig(egl_disp, attribs, &config, 1, &num_configs) || num_configs == 0) {
        LOGE("eglChooseConfig failed: 0x%x", eglGetError());
        return false;
    }

    egl_surf = eglCreateWindowSurface(egl_disp, config, g_window, nullptr);
    if (egl_surf == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }

    EGLint ctxAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    egl_ctx = eglCreateContext(egl_disp, config, EGL_NO_CONTEXT, ctxAttribs);
    if (egl_ctx == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);
    LOGI("EGL + OpenGL ES 3.0 ready");
    return true;
}

static void shutdown_egl() {
    if (egl_disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_disp, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_disp, egl_surf);
        eglTerminate(egl_disp);
    }
    egl_disp = EGL_NO_DISPLAY;
    egl_ctx  = EGL_NO_CONTEXT;
    egl_surf = EGL_NO_SURFACE;
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

static void save_frame_ppm(const char* filename) {
    unsigned char* pixels = new unsigned char[fb_width * fb_height * 4];
    glReadPixels(0, 0, fb_width, fb_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    FILE* f = fopen(filename, "wb");
    if (!f) { LOGE("Cannot write %s", filename); delete[] pixels; return; }

    fprintf(f, "P6\n%d %d\n255\n", fb_width, fb_height);
    for (int y = fb_height - 1; y >= 0; y--) {
        for (int x = 0; x < fb_width; x++) {
            int idx = (y * fb_width + x) * 4;
            fwrite(&pixels[idx + 0], 1, 1, f);
            fwrite(&pixels[idx + 1], 1, 1, f);
            fwrite(&pixels[idx + 2], 1, 1, f);
        }
    }
    fclose(f);
    delete[] pixels;
    LOGI("Frame saved: %s", filename);
}

// ============================================================
// 主入口
// ============================================================
int main(int argc, char** argv) {
    LOGI("Android ImGui Native Binary v2 (Screen Overlay) starting...");
    LOGI("Usage: %s [--frames N] [--dump-every N]", argv[0]);

    int total_frames  = 0;
    int dump_interval = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            total_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dump-every") == 0 && i + 1 < argc)
            dump_interval = atoi(argv[++i]);
    }

    // ============================================================
    // 1. 初始化 FunctionTable (自动解析符号)
    // ============================================================
    LOGI("Initializing SurfaceFlinger symbols...");
    FunctionTable fn;
    if (!fn.Init()) {
        LOGE("Failed to initialize FunctionTable");
        return 1;
    }

    // ============================================================
    // 2. 创建 SurfaceFlinger 叠加窗口
    // ============================================================
    if (!create_overlay_window(fn)) {
        LOGE("Failed to create overlay window. Exiting.");
        return 1;
    }

    // ============================================================
    // 3. 初始化 EGL
    // ============================================================
    if (!init_egl_with_window()) {
        LOGE("Failed to initialize EGL.");
        return 1;
    }

    // ============================================================
    // 4. 初始化 ImGui
    // ============================================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = (float)fb_width;
    io.DisplaySize.y = (float)fb_height;
    io.Fonts->AddFontDefault();

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("ImGui_ImplOpenGL3_Init failed");
        return 1;
    }

    // ============================================================
    // 5. 主渲染循环
    // ============================================================
    LOGI("Render loop starting (frames=%s). Ctrl+C to stop.",
         total_frames > 0 ? std::to_string(total_frames).c_str() : "infinite");

    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.2f, 1.00f);
    int frame = 0;

    while (true) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // Demo + 信息窗口
        ImGui::ShowDemoWindow(NULL);

        ImGui::Begin("Android Screen Overlay v2");
        ImGui::Text("ImGui on SurfaceFlinger overlay!");
        ImGui::Text("Android %zu | Resolution: %dx%d",
                     fn.androidVersion, fb_width, fb_height);
        ImGui::Text("Frame: %d | FPS: %.1f", frame + 1,
                     frame > 0 ? io.Framerate : 0.0f);
        ImGui::Separator();
        ImGui::Text("Method: SurfaceFlinger -> ANativeWindow -> EGL");
        ImGui::Text("No APK, No JNI. Needs root.");
        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        eglSwapBuffers(egl_disp, egl_surf);
        frame++;

        if (dump_interval > 0 && frame % dump_interval == 0) {
            char fname[128];
            snprintf(fname, sizeof(fname), "/sdcard/overlay_%04d.ppm", frame);
            save_frame_ppm(fname);
        }

        if (total_frames > 0 && frame >= total_frames) break;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    shutdown_egl();

    LOGI("Done. %d frames rendered.", frame);
    return 0;
}