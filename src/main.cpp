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

// 覆盖 ANativeWindowCreator.h 中的 LOGI/LOGE，改用 stdout 输出
#undef LOGI
#undef LOGE
#undef LOGW
#undef LOGD
#define LOGI(fmt, ...) fprintf(stdout, "[ImGui] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ImGui] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "[ImGui] " fmt "\n", ##__VA_ARGS__)
#define LOGD(fmt, ...) fprintf(stdout, "[ImGui] " fmt "\n", ##__VA_ARGS__)

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
    {
        uint64_t physId = 0;
        size_t count = 0;
        if (fn.getPhysicalDisplayIds(&physId, &count)) {
            LOGI("Found %zu physical display(s)", count);
            void* displayToken = nullptr;
            if (!fn.getPhysicalDisplayToken(physId, &displayToken) || !displayToken) {
                LOGE("getPhysicalDisplayToken failed");
                return false;
            }
            LOGI("Got display token via getPhysicalDisplayToken (token=%p)", displayToken);
        } else {
            LOGE("getPhysicalDisplayIds failed");
            return false;
        }
    }

    // --- 3. 分辨率 ---
    // getDisplayInfo 在 Android 15+ 中不存在，使用默认分辨率
    // ANativeWindow 创建后可以通过 ANativeWindow_getWidth/Height 获取真实尺寸
    fb_width  = 1080;
    fb_height = 1920;
    LOGI("Using default resolution: %dx%d (adjust after window creation)", fb_width, fb_height);

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
    // ABI: sp<SurfaceControl> createSurface(...) 使用隐藏参数返回
    // void realFn(sp<SurfaceControl>* result, self, name, w, h, format, flags, parent, meta, hint)
    char scSpBuf[sizeof(void*)] = {0};
    typedef void (*CreateSurfaceABI)(void* result, void* self, void* name,
        uint32_t w, uint32_t h, int32_t format, uint32_t flags,
        void* parent, void* meta, uint32_t* hint);
    auto createSurf = reinterpret_cast<CreateSurfaceABI>(fn.SCC_CreateSurface);
    createSurf((void*)scSpBuf, scc, str8, fb_width, fb_height,
        1,  // PIXEL_FORMAT_RGBA_8888
        1,  // ISurfaceComposerClient::eFXSurfaceBufferState
        nullptr, metaBuf, &transformHint);

    void* scSP = *(void**)scSpBuf;
    if (!scSP) {
        LOGE("createSurface returned null");
        return false;
    }
    LOGI("SurfaceControl created");

    // --- 7. 设置 Transaction ---
    char txnBuf[512] = {0};
    if (fn.Txn_Construct)
        fn.Txn_Construct(txnBuf);

    // SetLayer/SetTrustedOverlay 参数: const sp<SurfaceControl>&
    // ABI: 传入 sp<SurfaceControl>* (即指向 scSpBuf 的指针)
    if (fn.Txn_SetLayer)
        fn.Txn_SetLayer(txnBuf, (void*)scSpBuf, INT32_MAX);
    if (fn.Txn_Apply)
        fn.Txn_Apply(txnBuf, true, true);

    // --- 8. 获取 ANativeWindow ---
    // SC_GetSurface 返回 sp<Surface>，ABI 隐藏参数
    if (!fn.SC_GetSurface) {
        LOGE("SC_GetSurface not resolved");
        return false;
    }
    char surfSpBuf[sizeof(void*)] = {0};
    typedef void (*GetSurfaceABI)(void* result, void* self);
    auto getSurf = reinterpret_cast<GetSurfaceABI>(fn.SC_GetSurface);
    // SC_GetSurface 的 this = SurfaceControl*, sp<> 中存的就是裸指针
    void* surfaceControlObj = *(void**)scSpBuf;
    getSurf((void*)surfSpBuf, surfaceControlObj);

    void* surface = *(void**)surfSpBuf;
    if (!surface) {
        LOGE("SC_GetSurface returned null");
        return false;
    }

    g_window = reinterpret_cast<ANativeWindow*>(surface);
    ANativeWindow_acquire(g_window);

    // 通过 ANativeWindow 获取实际分辨率
    fb_width  = ANativeWindow_getWidth(g_window);
    fb_height = ANativeWindow_getHeight(g_window);
    LOGI("ANativeWindow: %p (actual: %dx%d)", (void*)g_window, fb_width, fb_height);
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