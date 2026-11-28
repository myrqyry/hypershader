#include "readback_fence.h"
#include <graphics/graphics.h>
#include <stddef.h>

#ifdef HS_HAVE_GL_READBACK_FENCE
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

bool hs_readback_fence_supported(void)
{
#ifdef HS_HAVE_GL_READBACK_FENCE
    /* OBS's desktop OpenGL backend requires GL 3.3. Verify the linked
     * dispatch library sees the same current context before using it. */
    return gs_get_device_type() == GS_DEVICE_OPENGL && glGetString(GL_VERSION) != NULL;
#else
    return false;
#endif
}

void *hs_readback_fence_insert(void)
{
#ifdef HS_HAVE_GL_READBACK_FENCE
    return (void *)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#else
    return NULL;
#endif
}

enum hs_readback_status hs_readback_fence_poll(void *fence, bool flush)
{
#ifdef HS_HAVE_GL_READBACK_FENCE
    if (fence) {
        /* No GPU completion wait, even on a very slow frame. */
        GLenum result = glClientWaitSync((GLsync)fence,
            flush ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
            return HS_READBACK_READY;
        if (result == GL_TIMEOUT_EXPIRED)
            return HS_READBACK_PENDING;
    }
#else
    (void)fence;
    (void)flush;
#endif
    return HS_READBACK_FAILED;
}

void hs_readback_fence_destroy(void *fence)
{
#ifdef HS_HAVE_GL_READBACK_FENCE
    if (fence)
        glDeleteSync((GLsync)fence);
#else
    (void)fence;
#endif
}
