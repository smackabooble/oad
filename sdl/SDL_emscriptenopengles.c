/*
  Simple DirectMedia Layer - Emscripten GL driver, WebGL-direct variant.

  Replaces the EGL-based implementation: Emscripten's EGL layer always runs on the main thread,
  which cannot work when the game runs in a worker (PROXY_TO_PTHREAD) and owns an OffscreenCanvas.
  This version creates the WebGL context directly on the calling thread and presents frames with
  emscripten_webgl_commit_frame().
*/
#include "../../SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_EMSCRIPTEN) && defined(SDL_VIDEO_OPENGL_EGL)

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES2/gl2.h>

#include "SDL_emscriptenvideo.h"
#include "SDL_emscriptenopengles.h"
#include "SDL_hints.h"

static int g_swap_interval = 1;

int Emscripten_GLES_LoadLibrary(_THIS, const char *path)
{
    (void)path;
    _this->gl_config.driver_path[0] = '\0';
    return 0;
}

void Emscripten_GLES_UnloadLibrary(_THIS)
{
    (void)_this;
}

void *Emscripten_GLES_GetProcAddress(_THIS, const char *proc)
{
    (void)_this;
    return emscripten_webgl_get_proc_address(proc);
}

SDL_GLContext Emscripten_GLES_CreateContext(_THIS, SDL_Window *window)
{
    SDL_WindowData *wdata = (SDL_WindowData *)window->driverdata;
    EmscriptenWebGLContextAttributes attrs;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle;

    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = _this->gl_config.alpha_size > 0;
    attrs.depth = _this->gl_config.depth_size > 0;
    attrs.stencil = _this->gl_config.stencil_size > 0;
    attrs.antialias = _this->gl_config.multisamplebuffers > 0;
    attrs.premultipliedAlpha = EM_TRUE;
    attrs.preserveDrawingBuffer = EM_FALSE;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attrs.majorVersion = (_this->gl_config.major_version >= 3) ? 2 : 1;
    attrs.explicitSwapControl = EM_TRUE;
    attrs.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;

    handle = emscripten_webgl_create_context(wdata->canvas_id, &attrs);
    if (handle <= 0) {
        SDL_SetError("emscripten_webgl_create_context failed: %d", (int)handle);
        return NULL;
    }
    if (emscripten_webgl_make_context_current(handle) != EMSCRIPTEN_RESULT_SUCCESS) {
        emscripten_webgl_destroy_context(handle);
        SDL_SetError("emscripten_webgl_make_context_current failed");
        return NULL;
    }
    return (SDL_GLContext)(intptr_t)handle;
}

int Emscripten_GLES_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
{
    (void)_this;
    (void)window;
    if (emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(intptr_t)context) != EMSCRIPTEN_RESULT_SUCCESS) {
        return SDL_SetError("emscripten_webgl_make_context_current failed");
    }
    return 0;
}

int Emscripten_GLES_SwapWindow(_THIS, SDL_Window *window)
{
    (void)_this;
    (void)window;
    /* Presents the frame when the context uses explicit swap control (OffscreenCanvas in a worker). */
    emscripten_webgl_commit_frame();
    return 0;
}

int Emscripten_GLES_SetSwapInterval(_THIS, int interval)
{
    (void)_this;
    g_swap_interval = interval;
    return 0;
}

int Emscripten_GLES_GetSwapInterval(_THIS)
{
    (void)_this;
    return g_swap_interval;
}

void Emscripten_GLES_DeleteContext(_THIS, SDL_GLContext context)
{
    (void)_this;
    emscripten_webgl_destroy_context((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)(intptr_t)context);
}

#endif /* SDL_VIDEO_DRIVER_EMSCRIPTEN && SDL_VIDEO_OPENGL_EGL */
