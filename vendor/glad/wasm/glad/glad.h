#pragma once

/*
 * GLAD compatibility stub for Emscripten / WebGL2
 *
 * Replaces the desktop GLAD loader when targeting WebAssembly.
 * Emscripten provides its own OpenGL ES 3.0 → WebGL2 translation layer,
 * so no explicit function-pointer loading is required.
 *
 * Provides:
 *   - All OpenGL ES 3.0 symbols (via <GLES3/gl3.h>)
 *   - GLAD type / struct shims so existing call-sites compile unchanged
 */

#ifndef __EMSCRIPTEN__
#  error "vendor/glad/wasm/glad/glad.h must only be included in Emscripten builds"
#endif

#include <emscripten.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLADloadproc — matches the desktop signature */
typedef void* (*GLADloadproc)(const char* name);

/* GLVersion — desktop GLAD exposes this as a global struct */
typedef struct { int major; int minor; } GLADVersionStruct;
static GLADVersionStruct GLVersion = { 3, 0 }; /* OpenGL ES 3.0 / WebGL2 */

/*
 * gladLoadGLLoader — no-op under Emscripten.
 * WebGL context is activated by the browser before main() runs.
 */
static inline int gladLoadGLLoader(GLADloadproc load)
{
    (void)load;
    return 1;
}

static inline int gladLoadGL(void)
{
    return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
