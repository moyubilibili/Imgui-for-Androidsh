#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

// ============================================================
// EGL / OpenGL ES 离屏渲染上下文
// ============================================================
static EGLDisplay  egl_disp  = EGL_NO_DISPLAY;
static EGLContext  egl_ctx   = EGL_NO_CONTEXT;
static EGLSurface  egl_surf  = EGL_NO_SURFACE;
static int fb_width  = 1280;
static int fb_height = 720;

static bool init_egl_offscreen() {
    EGLConfig  config;
    EGLint     num_configs;
    EGLint     major, minor;

    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed: 0x%x\n", eglGetError());
        return false;
    }

    if (!eglInitialize(egl_disp, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        return false;
    }
    fprintf(stdout, "[ImGui] EGL version: %d.%d\n", major, minor);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_BLUE_SIZE,       8,
        EGL_GREEN_SIZE,      8,
        EGL_RED_SIZE,        8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE
    };

    if (!eglChooseConfig(egl_disp, attribs, &config, 1, &num_configs) || num_configs == 0) {
        fprintf(stderr, "eglChooseConfig failed: 0x%x\n", eglGetError());
        return false;
    }

    const EGLint pbAttribs[] = {
        EGL_WIDTH,  fb_width,
        EGL_HEIGHT, fb_height,
        EGL_NONE
    };
    egl_surf = eglCreatePbufferSurface(egl_disp, config, pbAttribs);
    if (egl_surf == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreatePbufferSurface failed: 0x%x\n", eglGetError());
        return false;
    }

    EGLint ctxAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    egl_ctx = eglCreateContext(egl_disp, config, EGL_NO_CONTEXT, ctxAttribs);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError());
        return false;
    }

    eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);
    fprintf(stdout, "[ImGui] EGL + OpenGL ES 3.0 ready (%dx%d, offscreen)\n",
            fb_width, fb_height);
    return true;
}

static void shutdown_egl() {
    if (egl_disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_disp, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_disp, egl_surf);
        eglTerminate(egl_disp);
    }
}

// ============================================================
// 保存渲染帧到 PPM 文件
// ============================================================
static void save_frame_ppm(const char* filename) {
    unsigned char* pixels = new unsigned char[fb_width * fb_height * 4];

    glReadPixels(0, 0, fb_width, fb_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "[ImGui] Cannot write %s\n", filename);
        delete[] pixels;
        return;
    }

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
    fprintf(stdout, "[ImGui] Frame saved: %s (%dx%d)\n", filename, fb_width, fb_height);
}

// ============================================================
// 主入口
// ============================================================
int main(int argc, char** argv) {
    fprintf(stdout, "[ImGui] Android Native Binary starting...\n");
    fprintf(stdout, "[ImGui] Usage: %s [--frames N] [--width W] [--height H]\n", argv[0]);

    int total_frames = 120;
    int save_interval = 30;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            total_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            fb_width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            fb_height = atoi(argv[++i]);
    }

    // 1. 初始化 EGL 离屏渲染
    if (!init_egl_offscreen()) {
        fprintf(stderr, "[ImGui] EGL init failed, exiting.\n");
        return 1;
    }

    // 2. 初始化 ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = (float)fb_width;
    io.DisplaySize.y = (float)fb_height;

    io.Fonts->AddFontDefault();

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        fprintf(stderr, "[ImGui] ImGui_ImplOpenGL3_Init failed\n");
        return 1;
    }

    // 3. 渲染循环
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.2f, 1.00f);

    for (int frame = 0; frame < total_frames; frame++) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // ImGui 示例演示窗口
        ImGui::ShowDemoWindow(NULL);

        // 自定义信息窗口
        ImGui::Begin("Android Native Binary");
        ImGui::Text("ImGui running as native ELF binary on Android!");
        ImGui::Text("Frame: %d / %d", frame + 1, total_frames);
        ImGui::Text("Resolution: %dx%d", fb_width, fb_height);
        ImGui::Text("Platform: Android (arm64-v8a / x86_64)");
        ImGui::Separator();
        ImGui::Text("Renderer: EGL + OpenGL ES 3.0 (offscreen)");
        ImGui::Text("Build: NDK + CMake, static C++ runtime");
        if (frame > 0) {
            ImGui::Text("FPS: %.1f", io.Framerate);
        }
        ImGui::End();

        // 渲染
        ImGui::Render();
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        eglSwapBuffers(egl_disp, egl_surf);

        // 定期保存帧
        if (save_interval > 0 && (frame + 1) % save_interval == 0) {
            char fname[128];
            snprintf(fname, sizeof(fname),
                     "/sdcard/Android/data/com.imgui.binary/files/imgui_%04d.ppm",
                     frame + 1);
            save_frame_ppm(fname);
        }
    }

    // 4. 清理退出
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    shutdown_egl();

    fprintf(stdout, "[ImGui] Done. %d frames rendered.\n", total_frames);
    return 0;
}