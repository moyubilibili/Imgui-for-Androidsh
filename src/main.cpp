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

// ============================================================
// 日志宏
// ============================================================
#define LOGI(fmt, ...) fprintf(stdout, "[ImGui] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ImGui] " fmt "\n", ##__VA_ARGS__)

// ============================================================
// EGL / 窗口全局变量
// ============================================================
static EGLDisplay  egl_disp  = EGL_NO_DISPLAY;
static EGLContext  egl_ctx   = EGL_NO_CONTEXT;
static EGLSurface  egl_surf  = EGL_NO_SURFACE;
static ANativeWindow* g_window = nullptr;

// ============================================================
// SurfaceFlinger -> ANativeWindow 创建
// ============================================================
static bool create_overlay_window(android::detail::Functionals& fn,
                                   uint32_t& outW, uint32_t& outH)
{
    // --- 1. SurfaceComposerClient ---
    // 分配足够大的内存，调用构造函数
    char clientBuf[2048];
    memset(clientBuf, 0, sizeof(clientBuf));
    auto* scc = reinterpret_cast<void*>(clientBuf);

    if (!fn.SurfaceComposerClient__Constructor) {
        LOGE("SurfaceComposerClient::Constructor not resolved");
        return false;
    }
    fn.SurfaceComposerClient__Constructor(scc);
    LOGI("SurfaceComposerClient constructed");

    // --- 2. 获取显示 Token ---
    if (!fn.SurfaceComposerClient__GetInternalDisplayToken) {
        LOGE("GetInternalDisplayToken not resolved");
        return false;
    }
    auto displayToken = fn.SurfaceComposerClient__GetInternalDisplayToken();
    if (!displayToken) {
        LOGE("Failed to get internal display token");
        return false;
    }

    // --- 3. 获取显示信息 ---
    android::detail::ui::DisplayInfo dispInfo = {};
    if (fn.SurfaceComposerClient__GetDisplayInfo) {
        fn.SurfaceComposerClient__GetDisplayInfo(displayToken, &dispInfo);
        LOGI("DisplayInfo: %ux%u, fps=%.1f, density=%.1f",
             dispInfo.w, dispInfo.h, dispInfo.fps, dispInfo.density);
    } else {
        LOGE("GetDisplayInfo not available, using defaults");
        dispInfo.w = 1080;
        dispInfo.h = 1920;
    }

    outW = dispInfo.w;
    outH = dispInfo.h;

    // --- 4. 创建 String8 名称 ---
    char nameBuf[128];
    memset(nameBuf, 0, sizeof(nameBuf));
    android::detail::String8* str8 = reinterpret_cast<android::detail::String8*>(nameBuf);
    if (fn.String8__Constructor)
        fn.String8__Constructor(str8, "ImGuiOverlay");

    // --- 5. SurfaceControl 元数据 ---
    char metaBuf[256];
    memset(metaBuf, 0, sizeof(metaBuf));
    auto* layerMeta = reinterpret_cast<void*>(metaBuf);
    if (fn.LayerMetadata__Constructor)
        fn.LayerMetadata__Constructor(layerMeta);
    // 设置 Buffer 状态层
    if (fn.LayerMetadata__setInt32)
        fn.LayerMetadata__setInt32(layerMeta, 0, 1); // METADATA_BUFFER_STATE = 0, BUFFER_STATE_GRAPHICS_BUFFER = 1

    // --- 6. 创建 Surface ---
    if (!fn.SurfaceComposerClient__CreateSurface) {
        LOGE("CreateSurface not resolved");
        return false;
    }

    uint32_t transformHint = 0;
    auto surfaceControlSP = fn.SurfaceComposerClient__CreateSurface(
        scc,
        str8,
        dispInfo.w, dispInfo.h,
        1,         // PIXEL_FORMAT_RGBA_8888
        0x00000001, // ISurfaceComposerClient::eFXSurfaceBufferState
        nullptr,    // parentHandle
        layerMeta,
        &transformHint
    );

    if (!surfaceControlSP) {
        LOGE("createSurface failed - got null SurfaceControl");
        return false;
    }
    LOGI("SurfaceControl created, transform hint: %u", transformHint);

    // --- 7. 设置图层 ---
    char txnBuf[512];
    memset(txnBuf, 0, sizeof(txnBuf));
    auto* txn = reinterpret_cast<void*>(txnBuf);
    if (fn.SurfaceComposerClient__Transaction__Constructor)
        fn.SurfaceComposerClient__Transaction__Constructor(txn);

    // 设置 Layer=INT32_MAX (最顶层)
    if (fn.SurfaceComposerClient__Transaction__SetLayer)
        fn.SurfaceComposerClient__Transaction__SetLayer(txn, surfaceControlSP, INT32_MAX);

    // 设置 TrustedOverlay (Android 13+ 绕过 overlay 限制)
    if (fn.SurfaceComposerClient__Transaction__SetTrustedOverlay)
        fn.SurfaceComposerClient__Transaction__SetTrustedOverlay(txn, surfaceControlSP, true);

    // Apply transaction
    if (fn.SurfaceComposerClient__Transaction__Apply)
        fn.SurfaceComposerClient__Transaction__Apply(txn, true, true);

    // --- 8. 获取 ANativeWindow ---
    if (!fn.SurfaceControl__GetSurface) {
        LOGE("GetSurface not resolved");
        return false;
    }
    auto surfaceSP = fn.SurfaceControl__GetSurface(surfaceControlSP.get());
    if (!surfaceSP) {
        LOGE("GetSurface returned null");
        return false;
    }

    // android::Surface 可转换为 ANativeWindow
    g_window = reinterpret_cast<ANativeWindow*>(surfaceSP.get());
    ANativeWindow_acquire(g_window);

    LOGI("ANativeWindow created: %p (%ux%u)", (void*)g_window, dispInfo.w, dispInfo.h);
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

    // 选择 RGBA8888 + ES3 配置
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

    // 创建窗口表面
    egl_surf = eglCreateWindowSurface(egl_disp, config, g_window, nullptr);
    if (egl_surf == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }

    // OpenGL ES 3.0 上下文
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
    LOGI("EGL + OpenGL ES 3.0 initialized with ANativeWindow surface");
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

// ============================================================
// 保存帧到 PPM (可选)
// ============================================================
static void save_frame_ppm(const char* filename, int w, int h) {
    unsigned char* pixels = new unsigned char[w * h * 4];
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    FILE* f = fopen(filename, "wb");
    if (!f) {
        LOGE("Cannot write %s", filename);
        delete[] pixels;
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            fwrite(&pixels[idx + 0], 1, 1, f);
            fwrite(&pixels[idx + 1], 1, 1, f);
            fwrite(&pixels[idx + 2], 1, 1, f);
        }
    }
    fclose(f);
    delete[] pixels;
    LOGI("Frame saved: %s (%dx%d)", filename, w, h);
}

// ============================================================
// 主入口
// ============================================================
int main(int argc, char** argv) {
    LOGI("Android ImGui Native Binary (Screen Overlay) starting...");
    LOGI("Usage: %s [--frames N] [--dump-every N]", argv[0]);

    int total_frames  = 0;   // 0 = infinite
    int dump_interval = 0;   // 0 = no dump

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            total_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dump-every") == 0 && i + 1 < argc)
            dump_interval = atoi(argv[++i]);
    }

    // ============================================================
    // 1. 解析 SurfaceFlinger 符号
    // ============================================================
    android::detail::Functionals::SymbolMethod dl;
    dl.Open  = dlopen;
    dl.Find  = dlsym;
    dl.Close = dlclose;

    LOGI("Resolving SurfaceFlinger symbols (Android %zu)...", android::detail::Functionals(dl).systemVersion);
    
    // 这里创建 Functionals 实例会触发符号解析和打印日志
    // 实际符号保存在 Functionals 对象中供后续使用
    auto fn = android::detail::Functionals(dl);
    LOGI("Symbol resolution complete (Android %zu)", fn.systemVersion);

    // ============================================================
    // 2. 创建 SurfaceFlinger 窗口
    // ============================================================
    uint32_t fb_width = 0, fb_height = 0;
    if (!create_overlay_window(fn, fb_width, fb_height)) {
        LOGE("Failed to create overlay window. Exiting.");
        return 1;
    }
    LOGI("Overlay window created: %dx%d", fb_width, fb_height);

    // ============================================================
    // 3. 初始化 EGL
    // ============================================================
    if (!init_egl_with_window()) {
        LOGE("Failed to initialize EGL with window. Exiting.");
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
    LOGI("Starting render loop (frames=%s)...", 
         total_frames > 0 ? std::to_string(total_frames).c_str() : "infinite");
    LOGI("Press Ctrl+C on adb shell to stop.");

    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.2f, 1.00f);
    int frame = 0;

    while (true) {
        // 开始 ImGui 帧
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // 示例演示窗口
        ImGui::ShowDemoWindow(NULL);

        // 自定义窗口
        ImGui::Begin("Android Screen Overlay");
        ImGui::Text("ImGui rendering directly to screen via SurfaceFlinger!");
        ImGui::Text("Resolution: %dx%d", fb_width, fb_height);
        ImGui::Text("Frame: %d", frame + 1);
        if (frame > 0) ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Separator();
        ImGui::Text("Method: SurfaceFlinger -> ANativeWindow -> EGL");
        ImGui::Text("Symbols resolved from libgui.so at runtime");
        ImGui::Text("No APK, no Activity, no JNI needed.");
        ImGui::End();

        // 渲染
        ImGui::Render();
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        eglSwapBuffers(egl_disp, egl_surf);

        frame++;

        // 如果设置了 dump 间隔，保存帧到 /sdcard/
        if (dump_interval > 0 && frame % dump_interval == 0) {
            char fname[128];
            snprintf(fname, sizeof(fname), "/sdcard/overlay_%04d.ppm", frame);
            save_frame_ppm(fname, fb_width, fb_height);
        }

        // 如果设置了总帧数，到达后退出
        if (total_frames > 0 && frame >= total_frames) {
            LOGI("Reached target frame count (%d). Exiting.", total_frames);
            break;
        }
    }

    // ============================================================
    // 6. 清理
    // ============================================================
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    shutdown_egl();

    LOGI("Done. %d frames rendered to screen.", frame);
    return 0;
}