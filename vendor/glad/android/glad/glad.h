#pragma once

/*
 * GLAD compatibility stub for Android / OpenGL ES 3.0
 *
 * Replaces the desktop GLAD loader when targeting Android.
 * The Android NDK provides <GLES3/gl3.h> and the GPU driver exposes
 * all ES 3.0 entry points without a separate loading step.
 *
 * Provides:
 *   - All OpenGL ES 3.0 symbols (via <GLES3/gl3.h>)
 *   - GLAD type / struct shims so existing call-sites compile unchanged
 */

#ifndef __ANDROID__
#  error "vendor/glad/android/glad/glad.h must only be included in Android NDK builds"
#endif

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLADloadproc — matches the desktop signature */
typedef void* (*GLADloadproc)(const char* name);

/* GLVersion — desktop GLAD exposes this as a global struct */
typedef struct { int major; int minor; } GLADVersionStruct;
static GLADVersionStruct GLVersion = { 3, 0 }; /* OpenGL ES 3.0 */

/*
 * gladLoadGLLoader — no-op on Android.
 * All GLES3 entry points are resolved by the system linker via -lGLESv3.
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
