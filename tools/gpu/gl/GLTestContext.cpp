/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GLTestContext.h"

#include "GpuTimer.h"
#include "gl/GrGLUtil.h"

namespace {

class GLFenceSync : public sk_gpu_test::FenceSync {
public:
    static GLFenceSync* CreateIfSupported(const sk_gpu_test::GLTestContext*);

    sk_gpu_test::PlatformFence SK_WARN_UNUSED_RESULT insertFence() const override;
    bool waitFence(sk_gpu_test::PlatformFence fence) const override;
    void deleteFence(sk_gpu_test::PlatformFence fence) const override;

private:
    GLFenceSync(const sk_gpu_test::GLTestContext*, const char* ext = "");

    bool validate() { return fGLFenceSync && fGLClientWaitSync && fGLDeleteSync; }

    static constexpr GrGLenum GL_SYNC_GPU_COMMANDS_COMPLETE  = 0x9117;
    static constexpr GrGLenum GL_WAIT_FAILED                 = 0x911d;
    static constexpr GrGLbitfield GL_SYNC_FLUSH_COMMANDS_BIT = 0x00000001;

    typedef struct __GLsync *GLsync;
    GR_STATIC_ASSERT(sizeof(GLsync) <= sizeof(sk_gpu_test::PlatformFence));

    typedef GLsync (GR_GL_FUNCTION_TYPE* GLFenceSyncProc) (GrGLenum, GrGLbitfield);
    typedef GrGLenum (GR_GL_FUNCTION_TYPE* GLClientWaitSyncProc) (GLsync, GrGLbitfield, GrGLuint64);
    typedef GrGLvoid (GR_GL_FUNCTION_TYPE* GLDeleteSyncProc) (GLsync);

    GLFenceSyncProc        fGLFenceSync;
    GLClientWaitSyncProc   fGLClientWaitSync;
    GLDeleteSyncProc       fGLDeleteSync;

    typedef FenceSync INHERITED;
};

GLFenceSync* GLFenceSync::CreateIfSupported(const sk_gpu_test::GLTestContext* ctx) {
    SkAutoTDelete<GLFenceSync> ret;
    if (kGL_GrGLStandard == ctx->gl()->fStandard) {
        if (GrGLGetVersion(ctx->gl()) < GR_GL_VER(3,2) && !ctx->gl()->hasExtension("GL_ARB_sync")) {
            return nullptr;
        }
        ret.reset(new GLFenceSync(ctx));
    } else {
        if (!ctx->gl()->hasExtension("GL_APPLE_sync")) {
            return nullptr;
        }
        ret.reset(new GLFenceSync(ctx, "APPLE"));
    }
    return ret->validate() ? ret.release() : nullptr;
}

GLFenceSync::GLFenceSync(const sk_gpu_test::GLTestContext* ctx, const char* ext) {
    ctx->getGLProcAddress(&fGLFenceSync, "glFenceSync");
    ctx->getGLProcAddress(&fGLClientWaitSync, "glClientWaitSync");
    ctx->getGLProcAddress(&fGLDeleteSync, "glDeleteSync");
}

sk_gpu_test::PlatformFence GLFenceSync::insertFence() const {
    __GLsync* glsync = fGLFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    return reinterpret_cast<sk_gpu_test::PlatformFence>(glsync);
}

bool GLFenceSync::waitFence(sk_gpu_test::PlatformFence fence) const {
    GLsync glsync = reinterpret_cast<GLsync>(fence);
    return GL_WAIT_FAILED != fGLClientWaitSync(glsync, GL_SYNC_FLUSH_COMMANDS_BIT, -1);
}

void GLFenceSync::deleteFence(sk_gpu_test::PlatformFence fence) const {
    GLsync glsync = reinterpret_cast<GLsync>(fence);
    fGLDeleteSync(glsync);
}

class GLGpuTimer : public sk_gpu_test::GpuTimer {
public:
    static GLGpuTimer* CreateIfSupported(const sk_gpu_test::GLTestContext*);

    QueryStatus checkQueryStatus(sk_gpu_test::PlatformTimerQuery) override;
    std::chrono::nanoseconds getTimeElapsed(sk_gpu_test::PlatformTimerQuery) override;
    void deleteQuery(sk_gpu_test::PlatformTimerQuery) override;

private:
    GLGpuTimer(bool disjointSupport, const sk_gpu_test::GLTestContext*, const char* ext = "");

    bool validate() const;

    sk_gpu_test::PlatformTimerQuery onQueueTimerStart() const override;
    void onQueueTimerStop(sk_gpu_test::PlatformTimerQuery) const override;

    static constexpr GrGLenum GL_QUERY_RESULT            = 0x8866;
    static constexpr GrGLenum GL_QUERY_RESULT_AVAILABLE  = 0x8867;
    static constexpr GrGLenum GL_TIME_ELAPSED            = 0x88bf;
    static constexpr GrGLenum GL_GPU_DISJOINT            = 0x8fbb;

    typedef void (GR_GL_FUNCTION_TYPE* GLGetIntegervProc) (GrGLenum, GrGLint*);
    typedef void (GR_GL_FUNCTION_TYPE* GLGenQueriesProc) (GrGLsizei, GrGLuint*);
    typedef void (GR_GL_FUNCTION_TYPE* GLDeleteQueriesProc) (GrGLsizei, const GrGLuint*);
    typedef void (GR_GL_FUNCTION_TYPE* GLBeginQueryProc) (GrGLenum, GrGLuint);
    typedef void (GR_GL_FUNCTION_TYPE* GLEndQueryProc) (GrGLenum);
    typedef void (GR_GL_FUNCTION_TYPE* GLGetQueryObjectuivProc) (GrGLuint, GrGLenum, GrGLuint*);
    typedef void (GR_GL_FUNCTION_TYPE* GLGetQueryObjectui64vProc) (GrGLuint, GrGLenum, GrGLuint64*);

    GLGetIntegervProc           fGLGetIntegerv;
    GLGenQueriesProc            fGLGenQueries;
    GLDeleteQueriesProc         fGLDeleteQueries;
    GLBeginQueryProc            fGLBeginQuery;
    GLEndQueryProc              fGLEndQuery;
    GLGetQueryObjectuivProc     fGLGetQueryObjectuiv;
    GLGetQueryObjectui64vProc   fGLGetQueryObjectui64v;


    typedef sk_gpu_test::GpuTimer INHERITED;
};

GLGpuTimer* GLGpuTimer::CreateIfSupported(const sk_gpu_test::GLTestContext* ctx) {
    SkAutoTDelete<GLGpuTimer> ret;
    const GrGLInterface* gl = ctx->gl();
    if (gl->fExtensions.has("GL_EXT_disjoint_timer_query")) {
        ret.reset(new GLGpuTimer(true, ctx, "EXT"));
    } else if (kGL_GrGLStandard == gl->fStandard &&
               (GrGLGetVersion(gl) > GR_GL_VER(3,3) || gl->fExtensions.has("GL_ARB_timer_query"))) {
        ret.reset(new GLGpuTimer(false, ctx));
    } else if (gl->fExtensions.has("GL_EXT_timer_query")) {
        ret.reset(new GLGpuTimer(false, ctx, "EXT"));
    }
    return ret && ret->validate() ? ret.release() : nullptr;
}

GLGpuTimer::GLGpuTimer(bool disjointSupport, const sk_gpu_test::GLTestContext* ctx, const char* ext)
    : INHERITED(disjointSupport) {
    ctx->getGLProcAddress(&fGLGetIntegerv, "glGetIntegerv");
    ctx->getGLProcAddress(&fGLGenQueries, "glGenQueries", ext);
    ctx->getGLProcAddress(&fGLDeleteQueries, "glDeleteQueries", ext);
    ctx->getGLProcAddress(&fGLBeginQuery, "glBeginQuery", ext);
    ctx->getGLProcAddress(&fGLEndQuery, "glEndQuery", ext);
    ctx->getGLProcAddress(&fGLGetQueryObjectuiv, "glGetQueryObjectuiv", ext);
    ctx->getGLProcAddress(&fGLGetQueryObjectui64v, "glGetQueryObjectui64v", ext);
}

bool GLGpuTimer::validate() const {
    return fGLGetIntegerv && fGLGenQueries && fGLDeleteQueries && fGLBeginQuery && fGLEndQuery &&
           fGLGetQueryObjectuiv && fGLGetQueryObjectui64v;
}

sk_gpu_test::PlatformTimerQuery GLGpuTimer::onQueueTimerStart() const {
    GrGLuint queryID;
    fGLGenQueries(1, &queryID);
    if (!queryID) {
        return sk_gpu_test::kInvalidTimerQuery;
    }
    if (this->disjointSupport()) {
        // Clear the disjoint flag.
        GrGLint disjoint;
        fGLGetIntegerv(GL_GPU_DISJOINT, &disjoint);
    }
    fGLBeginQuery(GL_TIME_ELAPSED, queryID);
    return static_cast<sk_gpu_test::PlatformTimerQuery>(queryID);
}

void GLGpuTimer::onQueueTimerStop(sk_gpu_test::PlatformTimerQuery platformTimer) const {
    if (sk_gpu_test::kInvalidTimerQuery == platformTimer) {
        return;
    }
    fGLEndQuery(GL_TIME_ELAPSED);
}

sk_gpu_test::GpuTimer::QueryStatus
GLGpuTimer::checkQueryStatus(sk_gpu_test::PlatformTimerQuery platformTimer) {
    const GrGLuint queryID = static_cast<GrGLuint>(platformTimer);
    if (!queryID) {
        return QueryStatus::kInvalid;
    }
    GrGLuint available = 0;
    fGLGetQueryObjectuiv(queryID, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) {
        return QueryStatus::kPending;
    }
    if (this->disjointSupport()) {
        GrGLint disjoint = 1;
        fGLGetIntegerv(GL_GPU_DISJOINT, &disjoint);
        if (disjoint) {
            return QueryStatus::kDisjoint;
        }
    }
    return QueryStatus::kAccurate;
}

std::chrono::nanoseconds GLGpuTimer::getTimeElapsed(sk_gpu_test::PlatformTimerQuery platformTimer) {
    SkASSERT(this->checkQueryStatus(platformTimer) >= QueryStatus::kDisjoint);
    const GrGLuint queryID = static_cast<GrGLuint>(platformTimer);
    GrGLuint64 nanoseconds;
    fGLGetQueryObjectui64v(queryID, GL_QUERY_RESULT, &nanoseconds);
    return std::chrono::nanoseconds(nanoseconds);
}

void GLGpuTimer::deleteQuery(sk_gpu_test::PlatformTimerQuery platformTimer) {
    const GrGLuint queryID = static_cast<GrGLuint>(platformTimer);
    fGLDeleteQueries(1, &queryID);
}

GR_STATIC_ASSERT(sizeof(GrGLuint) <= sizeof(sk_gpu_test::PlatformTimerQuery));

}  // anonymous namespace

namespace sk_gpu_test {

GLTestContext::GLTestContext() : TestContext() {}

GLTestContext::~GLTestContext() {
    SkASSERT(nullptr == fGL.get());
}

void GLTestContext::init(const GrGLInterface* gl, FenceSync* fenceSync) {
    SkASSERT(!fGL.get());
    fGL.reset(gl);
    fFenceSync = fenceSync ? fenceSync : GLFenceSync::CreateIfSupported(this);
    fGpuTimer = GLGpuTimer::CreateIfSupported(this);
}

void GLTestContext::teardown() {
    fGL.reset(nullptr);
    INHERITED::teardown();
}

void GLTestContext::testAbandon() {
    INHERITED::testAbandon();
    if (fGL) {
        fGL->abandon();
    }
}

void GLTestContext::submit() {
    if (fGL) {
        GR_GL_CALL(fGL.get(), Flush());
    }
}

void GLTestContext::finish() {
    if (fGL) {
        GR_GL_CALL(fGL.get(), Finish());
    }
}

GrGLint GLTestContext::createTextureRectangle(int width, int height, GrGLenum internalFormat,
                                          GrGLenum externalFormat, GrGLenum externalType,
                                          GrGLvoid* data) {
    if (!(kGL_GrGLStandard == fGL->fStandard && GrGLGetVersion(fGL) >= GR_GL_VER(3, 1)) &&
        !fGL->fExtensions.has("GL_ARB_texture_rectangle")) {
        return 0;
    }

    if  (GrGLGetGLSLVersion(fGL) < GR_GLSL_VER(1, 40)) {
        return 0;
    }

    GrGLuint id;
    GR_GL_CALL(fGL, GenTextures(1, &id));
    GR_GL_CALL(fGL, BindTexture(GR_GL_TEXTURE_RECTANGLE, id));
    GR_GL_CALL(fGL, TexParameteri(GR_GL_TEXTURE_RECTANGLE, GR_GL_TEXTURE_MAG_FILTER,
                                  GR_GL_NEAREST));
    GR_GL_CALL(fGL, TexParameteri(GR_GL_TEXTURE_RECTANGLE, GR_GL_TEXTURE_MIN_FILTER,
                                  GR_GL_NEAREST));
    GR_GL_CALL(fGL, TexParameteri(GR_GL_TEXTURE_RECTANGLE, GR_GL_TEXTURE_WRAP_S,
                                  GR_GL_CLAMP_TO_EDGE));    
    GR_GL_CALL(fGL, TexParameteri(GR_GL_TEXTURE_RECTANGLE, GR_GL_TEXTURE_WRAP_T,
                                  GR_GL_CLAMP_TO_EDGE));
    GR_GL_CALL(fGL, TexImage2D(GR_GL_TEXTURE_RECTANGLE, 0, internalFormat, width, height, 0,
                               externalFormat, externalType, data));
    return id;
}
}  // namespace sk_gpu_test
