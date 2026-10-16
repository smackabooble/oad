#include "../../SDL_internal.h"

#ifndef SDL_emscriptenopengles_h_
#define SDL_emscriptenopengles_h_

#if defined(SDL_VIDEO_DRIVER_EMSCRIPTEN) && defined(SDL_VIDEO_OPENGL_EGL)

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

extern int Emscripten_GLES_LoadLibrary(_THIS, const char *path);
extern void Emscripten_GLES_UnloadLibrary(_THIS);
extern void *Emscripten_GLES_GetProcAddress(_THIS, const char *proc);
extern SDL_GLContext Emscripten_GLES_CreateContext(_THIS, SDL_Window *window);
extern int Emscripten_GLES_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context);
extern int Emscripten_GLES_SwapWindow(_THIS, SDL_Window *window);
extern int Emscripten_GLES_SetSwapInterval(_THIS, int interval);
extern int Emscripten_GLES_GetSwapInterval(_THIS);
extern void Emscripten_GLES_DeleteContext(_THIS, SDL_GLContext context);

#endif

#endif /* SDL_emscriptenopengles_h_ */
