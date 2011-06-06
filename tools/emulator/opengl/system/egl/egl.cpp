/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "HostConnection.h"
#include "ThreadInfo.h"
#include "eglDisplay.h"
#include "egl_ftable.h"
#include <cutils/log.h>

#include <private/ui/android_natives_priv.h>

template<typename T>
static T setError(GLint error, T returnValue) {
	getEGLThreadInfo()->eglError = error;
	return returnValue;
}

#define RETURN_ERROR(ret,err)           \
	getEGLThreadInfo()->eglError = err;	\
    return ret;

#define VALIDATE_CONFIG(cfg,ret) \
    if(((int)cfg<0)||((int)cfg>s_display.getNumConfigs())) { \
		RETURN_ERROR(ret,EGL_BAD_CONFIG); \
    }

#define VALIDATE_DISPLAY(dpy,ret) \
    if ((dpy) != (EGLDisplay)&s_display) { \
        getEGLThreadInfo()->eglError = EGL_BAD_DISPLAY; \
        return ret; \
    }

#define VALIDATE_DISPLAY_INIT(dpy,ret) \
    VALIDATE_DISPLAY(dpy, ret)	\
    if (!s_display.initialized()) {		\
		getEGLThreadInfo()->eglError = EGL_NOT_INITIALIZED;	\
        return ret; \
    }

#define DEFINE_HOST_CONNECTION \
    HostConnection *hostCon = HostConnection::get(); \
    renderControl_encoder_context_t *rcEnc = (hostCon ? hostCon->rcEncoder() : NULL)

#define DEFINE_AND_VALIDATE_HOST_CONNECTION(ret) \
    HostConnection *hostCon = HostConnection::get(); \
    if (!hostCon) { \
        LOGE("egl: Failed to get host connection\n"); \
        return ret; \
    } \
    renderControl_encoder_context_t *rcEnc = hostCon->rcEncoder(); \
    if (!rcEnc) { \
        LOGE("egl: Failed to get renderControl encoder context\n"); \
        return ret; \
    }

#define VALIDATE_CONTEXT_RETURN(context,ret)        \
	if (!context) {							        \
		RETURN_ERROR(ret,EGL_BAD_CONTEXT);	\
	}

#define VALIDATE_SURFACE_RETURN(surface, ret)	\
	if (surface != EGL_NO_SURFACE) {	\
		egl_surface_t* s( static_cast<egl_surface_t*>(surface) );	\
		if (!s->isValid())	\
			return setError(EGL_BAD_SURFACE, EGL_FALSE);	\
		if (s->dpy != (EGLDisplay)&s_display)	\
			return setError(EGL_BAD_DISPLAY, EGL_FALSE);	\
	}


// ----------------------------------------------------------------------------
//EGLContext_t

struct EGLContext_t {

	//XXX: do we need this?
	enum {
		IS_CURRENT      =   0x00010000,
		NEVER_CURRENT   =   0x00020000
	};
	
	EGLContext_t(EGLDisplay dpy, EGLConfig config) : dpy(dpy), config(config), read(EGL_NO_SURFACE), draw(EGL_NO_SURFACE), rcContext(0) {};
	~EGLContext_t(){}; 
//	EGLBoolean	rcCreate();
//	EGLBoolean	rcDestroy();
	uint32_t            flags; //XXX: do we need this?
	EGLDisplay          dpy;
	EGLConfig           config;
	EGLSurface          read;
	EGLSurface          draw;
	uint32_t 			rcContext;
};

// ----------------------------------------------------------------------------
//egl_surface_t

//we don't need to handle depth since it's handled when window created on the host

struct egl_surface_t {

	EGLDisplay          dpy;
	EGLConfig           config;
	//EGLContext          ctx;

	egl_surface_t(EGLDisplay dpy, EGLConfig config);
	virtual     ~egl_surface_t();
	virtual 	EGLBoolean 		rcCreate() = 0;
	virtual 	EGLBoolean 		rcDestroy() = 0;
	void 		setRcSurface(uint32_t handle){ rcSurface = handle; }
	uint32_t 	getRcSurface(){ return rcSurface; }

	virtual 	EGLBoolean	isValid(){ return valid; }
	virtual     EGLint      getWidth() const = 0;
	virtual     EGLint      getHeight() const = 0;

protected:
	EGLBoolean			valid;
	uint32_t 			rcSurface; //handle to surface created via remote control
};

egl_surface_t::egl_surface_t(EGLDisplay dpy, EGLConfig config)
    : dpy(dpy), config(config), valid(EGL_FALSE), rcSurface(0)
{
}

egl_surface_t::~egl_surface_t()
{
}

// ----------------------------------------------------------------------------
// egl_window_surface_t

struct egl_window_surface_t : public egl_surface_t {
    
	ANativeWindow* 	nativeWindow;
	int width;
	int height;

	virtual     EGLint      getWidth() const    { return width;  }
	virtual     EGLint      getHeight() const   { return height; }

	egl_window_surface_t(
			EGLDisplay dpy, EGLConfig config,
			ANativeWindow* window);

	~egl_window_surface_t();
	virtual 	EGLBoolean 	rcCreate();
	virtual 	EGLBoolean 	rcDestroy();

};


egl_window_surface_t::egl_window_surface_t (
			EGLDisplay dpy, EGLConfig config,
			ANativeWindow* window)
	: egl_surface_t(dpy, config), 
	nativeWindow(window)
{
	// keep a reference on the window
	nativeWindow->common.incRef(&nativeWindow->common);
	nativeWindow->query(nativeWindow, NATIVE_WINDOW_WIDTH, &width);
	nativeWindow->query(nativeWindow, NATIVE_WINDOW_HEIGHT, &height);

}

egl_window_surface_t::~egl_window_surface_t() {
	nativeWindow->common.decRef(&nativeWindow->common);
}

EGLBoolean egl_window_surface_t::rcCreate() 
{
	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
	uint32_t rcSurface = rcEnc->rcCreateWindowSurface(rcEnc, (uint32_t)config, getWidth(), getHeight()); 
	if (!rcSurface) {
		LOGE("rcCreateWindowSurface returned 0");
		return EGL_FALSE;
	}
	valid = EGL_TRUE;
	return EGL_TRUE;
}

EGLBoolean egl_window_surface_t::rcDestroy()
{
	if (!rcSurface) {
		LOGE("rcDestroy called on invalid rcSurface");
		return EGL_FALSE;
	}
	
	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
	rcEnc->rcDestroyWindowSurface(rcEnc, rcSurface);
	rcSurface = 0;

	return EGL_TRUE;
}

// ----------------------------------------------------------------------------
//egl_pbuffer_surface_t

struct egl_pbuffer_surface_t : public egl_surface_t {
     
	int width;
	int height;
	GLenum	format;

	virtual     EGLint      getWidth() const    { return width;  }
	virtual     EGLint      getHeight() const   { return height; }
	
	egl_pbuffer_surface_t(
			EGLDisplay dpy, EGLConfig config, 
			int32_t w, int32_t h, GLenum format);

	virtual ~egl_pbuffer_surface_t();
	virtual 	EGLBoolean 	rcCreate();
	virtual 	EGLBoolean 	rcDestroy();

	uint32_t	getRcColorBuffer(){ return rcColorBuffer; }
	void 		setRcColorBuffer(uint32_t colorBuffer){ rcColorBuffer = colorBuffer; }	
private:
	uint32_t rcColorBuffer;
};

egl_pbuffer_surface_t::egl_pbuffer_surface_t(
		EGLDisplay dpy, EGLConfig config,
		int32_t w, int32_t h, GLenum pixelFormat)
	: egl_surface_t(dpy, config), 
	width(w), height(h), format(pixelFormat)
{
}

egl_pbuffer_surface_t::~egl_pbuffer_surface_t()
{
	rcColorBuffer = 0;
}

EGLBoolean egl_pbuffer_surface_t::rcCreate() 
{
	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
	rcSurface = rcEnc->rcCreateWindowSurface(rcEnc, (uint32_t)config, getWidth(), getHeight()); 
	if (!rcSurface) {
		LOGE("rcCreateWindowSurface returned 0");
		return EGL_FALSE;
	}
	rcColorBuffer = rcEnc->rcCreateColorBuffer(rcEnc, getWidth(), getHeight(), format);
	if (!rcColorBuffer) {
		LOGE("rcCreateColorBuffer returned 0");
		return EGL_FALSE;
	}

	valid = EGL_TRUE;
	return EGL_TRUE;
}

EGLBoolean egl_pbuffer_surface_t::rcDestroy()
{
	if ((!rcSurface)||(!rcColorBuffer)) {
		LOGE("rcDestroy called on invalid rcSurface");
		return EGL_FALSE;
	}
	
	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
	rcEnc->rcDestroyWindowSurface(rcEnc, rcSurface);
	rcEnc->rcDestroyColorBuffer(rcEnc, rcColorBuffer);
	rcSurface = 0;

	return EGL_TRUE;
}


// ----------------------------------------------------------------------------

// The one and only supported display object.
static eglDisplay s_display;

static EGLClient_eglInterface s_eglIface = {
    getThreadInfo: getEGLThreadInfo
};
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    //
    // we support only EGL_DEFAULT_DISPLAY.
    //
    if (display_id != EGL_DEFAULT_DISPLAY) {
        return EGL_NO_DISPLAY;
    }

    return (EGLDisplay)&s_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    VALIDATE_DISPLAY(dpy,EGL_FALSE);

    if (!s_display.initialize(&s_eglIface)) {
        return EGL_FALSE;
    }

    *major = s_display.getVersionMajor();
    *minor = s_display.getVersionMinor();
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);

    s_display.terminate();
    return EGL_TRUE;
}

EGLint eglGetError()
{
    return getEGLThreadInfo()->eglError;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    // search in EGL function table
    for (int i=0; i<egl_num_funcs; i++) {
        if (!strcmp(egl_funcs_by_name[i].name, procname)) {
            return (__eglMustCastToProperFunctionPointerType)egl_funcs_by_name[i].proc;
        }
    }

    //
    // Make sure display is initialized before searching in client APIs
    //
    if (!s_display.initialized()) {
        if (!s_display.initialize(&s_eglIface)) {
            return NULL;
        }
    }

    // look in gles
    void *proc = s_display.gles_iface()->getProcAddress( procname );
    if (proc != NULL) {
        return (__eglMustCastToProperFunctionPointerType)proc;
    }

    // look in gles2
    if (s_display.gles2_iface() != NULL) {
        proc = s_display.gles2_iface()->getProcAddress( procname );
        if (proc != NULL) {
            return (__eglMustCastToProperFunctionPointerType)proc;
        }
    }

    // Fail - function not found.
    return NULL;
}

const char* eglQueryString(EGLDisplay dpy, EGLint name)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
    
    return s_display.queryString(name);
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);

	if(!num_config) {
		RETURN_ERROR(EGL_FALSE,EGL_BAD_PARAMETER);
	}

	GLint numConfigs = s_display.getNumConfigs();
	if (!configs) {
		*num_config = numConfigs;
		return EGL_TRUE;
	}

	int i=0;
	for (i=0 ; i<numConfigs && i<config_size ; i++) {
		*configs++ = (EGLConfig)i;
	}
	*num_config = i;
	return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);

	//TODO: Why should we have 2 pieces of code for this on the server and on the client?
	return 0;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
	VALIDATE_CONFIG(config, EGL_FALSE);
	
	if (s_display.getConfigAttrib(config, attribute, value))
	{
		return EGL_TRUE;
	}
	else 
	{
		RETURN_ERROR(EGL_FALSE, EGL_BAD_ATTRIBUTE);
	}
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
	VALIDATE_CONFIG(config, EGL_FALSE);
	if (win == 0)
	{
		return setError(EGL_BAD_MATCH, EGL_NO_SURFACE);
	}

	EGLint surfaceType;
	if (s_display.getConfigAttrib(config, EGL_SURFACE_TYPE, &surfaceType) == EGL_FALSE)	return EGL_FALSE;

	if (!(surfaceType & EGL_WINDOW_BIT)) {
		return setError(EGL_BAD_MATCH, EGL_NO_SURFACE);
	}

	    
	if (static_cast<ANativeWindow*>(win)->common.magic != ANDROID_NATIVE_WINDOW_MAGIC) {
		return setError(EGL_BAD_NATIVE_WINDOW, EGL_NO_SURFACE);
	}

	egl_surface_t* surface;
	surface = new egl_window_surface_t(&s_display, config, static_cast<ANativeWindow*>(win));
	if (!surface) 
		return setError(EGL_BAD_ALLOC, EGL_NO_SURFACE);
	if (!surface->rcCreate()) {
		delete surface;
		return setError(EGL_BAD_ALLOC, EGL_NO_SURFACE);
	}

	return surface;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
	VALIDATE_CONFIG(config, EGL_FALSE);

	EGLint surfaceType;
	if (s_display.getConfigAttrib(config, EGL_SURFACE_TYPE, &surfaceType) == EGL_FALSE)	return EGL_FALSE;

	if (!(surfaceType & EGL_PBUFFER_BIT)) {
		return setError(EGL_BAD_MATCH, EGL_NO_SURFACE);
	}

	int32_t w = 0;
	int32_t h = 0;
	while (attrib_list[0]) {
		if (attrib_list[0] == EGL_WIDTH)  w = attrib_list[1];
		if (attrib_list[0] == EGL_HEIGHT) h = attrib_list[1];
		attrib_list+=2;
	}

	GLenum pixelFormat;
	if (s_display.getConfigPixelFormat(config, &pixelFormat) == EGL_FALSE)
		return setError(EGL_BAD_MATCH, EGL_NO_SURFACE);
	
	egl_surface_t* surface = new egl_pbuffer_surface_t(dpy, config, w, h, pixelFormat);
	if (!surface) 
		return setError(EGL_BAD_ALLOC, EGL_NO_SURFACE);
	if (!surface->rcCreate()) {
		delete surface;
		return setError(EGL_BAD_ALLOC, EGL_NO_SURFACE);
	}

	return surface;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list)
{
	//XXX: Pixmap not supported
	return EGL_NO_SURFACE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface eglSurface)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
	VALIDATE_SURFACE_RETURN(eglSurface, EGL_FALSE);

	egl_surface_t* surface( static_cast<egl_surface_t*>(eglSurface) );
	
	surface->rcDestroy();
	delete surface;

	return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	//TODO
	return 0;
}

EGLBoolean eglBindAPI(EGLenum api)
{
	if (api != EGL_OPENGL_ES_API)
		return setError(EGL_BAD_PARAMETER, EGL_FALSE);
	return EGL_TRUE;
}

EGLenum eglQueryAPI()
{
	return EGL_OPENGL_ES_API;
}

EGLBoolean eglWaitClient()
{	//TODO
	return 0;

}

EGLBoolean eglReleaseThread()
{
	//TODO
	return 0;
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
	//TODO
	return 0;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	//TODO
	return 0;
}

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	//TODO
	return 0;
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	//TODO
	return 0;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	//TODO
	return 0;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	VALIDATE_DISPLAY_INIT(dpy, EGL_NO_CONTEXT);
	VALIDATE_CONFIG(config, EGL_NO_CONTEXT);


	EGLint version = 1; //default
	while (attrib_list[0]) {
		if (attrib_list[0] == EGL_CONTEXT_CLIENT_VERSION) version = attrib_list[1];
		attrib_list+=2;
	}

	uint32_t rcShareCtx = 0;
	if (share_context) {
		EGLContext_t * shareCtx = static_cast<EGLContext_t*>(share_context);
		rcShareCtx = shareCtx->rcContext;
		if (shareCtx->dpy != dpy) 
			return setError(EGL_BAD_MATCH, EGL_NO_CONTEXT);
	}

	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_NO_CONTEXT);
	uint32_t rcContext = rcEnc->rcCreateContext(rcEnc, (uint32_t)config, rcShareCtx, version);
	if (!rcContext) {
		LOGE("rcCreateContext returned 0");
		return setError(EGL_BAD_ALLOC, EGL_NO_CONTEXT);
	}
	
	EGLContext_t * context = new EGLContext_t(dpy, config);
	if (!context) 
		return setError(EGL_BAD_ALLOC, EGL_NO_CONTEXT);

	context->rcContext = rcContext;


	return context;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
	VALIDATE_CONTEXT_RETURN(ctx, EGL_FALSE);

	EGLContext_t * context = static_cast<EGLContext_t*>(ctx);
	if (context->rcContext) {
		DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
		rcEnc->rcDestroyContext(rcEnc, context->rcContext);
		context->rcContext = 0;
	}

	if (getEGLThreadInfo()->currentContext == context)
		getEGLThreadInfo()->currentContext = NULL; 

	delete context;
	return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
	VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
	VALIDATE_SURFACE_RETURN(draw, EGL_FALSE);
	VALIDATE_SURFACE_RETURN(read, EGL_FALSE);

	if ((read == EGL_NO_SURFACE && draw == EGL_NO_SURFACE) && (ctx != EGL_NO_CONTEXT))
		return setError(EGL_BAD_MATCH, EGL_FALSE);
	if ((read != EGL_NO_SURFACE || draw != EGL_NO_SURFACE) && (ctx == EGL_NO_CONTEXT))	
		return setError(EGL_BAD_MATCH, EGL_FALSE);

	EGLContext_t * context = static_cast<EGLContext_t*>(ctx);
	uint32_t ctxHandle = (context) ? context->rcContext : 0;
	egl_surface_t * drawSurf = static_cast<egl_surface_t *>(draw);
	uint32_t drawHandle = (drawSurf) ? drawSurf->getRcSurface() : 0;
	egl_surface_t * readSurf = static_cast<egl_surface_t *>(read);
	uint32_t readHandle = (readSurf) ? readSurf->getRcSurface() : 0;

	DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
	if (rcEnc->rcMakeCurrent(rcEnc, ctxHandle, drawHandle, readHandle) == EGL_FALSE) {
		LOGE("rcMakeCurrent returned EGL_FALSE");
		return setError(EGL_BAD_CONTEXT, EGL_FALSE);
	}

	//Now make the local bind
	if (context) {
		context->draw = draw;
		context->read = read;
	}
	//Now make current
	getEGLThreadInfo()->currentContext = context;

	return EGL_TRUE;
}

EGLContext eglGetCurrentContext()
{
	return getEGLThreadInfo()->currentContext;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
	EGLContext_t * context = getEGLThreadInfo()->currentContext;
	if (!context)
		return EGL_NO_SURFACE; //not an error

	switch (readdraw) {
		case EGL_READ:
			return context->read;
		case EGL_DRAW:
			return context->draw;
		default:
			return setError(EGL_BAD_PARAMETER, EGL_NO_SURFACE);
	}
}

EGLDisplay eglGetCurrentDisplay()
{
	EGLContext_t * context = getEGLThreadInfo()->currentContext;
	if (!context)
		return EGL_NO_DISPLAY; //not an error

	return context->dpy;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	//TODO
	return 0;
}

EGLBoolean eglWaitGL()
{
	//TODO
	return 0;
}

EGLBoolean eglWaitNative(EGLint engine)
{
	//TODO
	return 0;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	//TODO
	return 0;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	//TODO
	return 0;
}

EGLBoolean eglLockSurfaceKHR(EGLDisplay display, EGLSurface surface, const EGLint *attrib_list)
{
	//TODO
	return 0;
}

EGLBoolean eglUnlockSurfaceKHR(EGLDisplay display, EGLSurface surface)
{
	//TODO
	return 0;
}

EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
	//TODO
	return 0;
}

EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
	//TODO
	return 0;
}

EGLSyncKHR eglCreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list)
{
	//TODO
	return 0;
}

EGLBoolean eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
{
	//TODO
	return 0;
}

EGLint eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout)
{
	//TODO
	return 0;
}

EGLBoolean eglSignalSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLenum mode)
{
	//TODO
	return 0;
}

EGLBoolean eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLint *value)
{
	//TODO
	return 0;
}

EGLBoolean eglSetSwapRectangleANDROID(EGLDisplay dpy, EGLSurface draw, EGLint left, EGLint top, EGLint width, EGLint height)
{
	//TODO
	return 0;
}
