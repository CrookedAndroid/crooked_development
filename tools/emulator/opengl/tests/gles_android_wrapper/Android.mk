LOCAL_PATH := $(call my-dir)

##### libGLES_emul.so ###########
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=  \
        egl.cpp \
        egl_dispatch.cpp \
        gles.cpp \
        gles_dispatch.cpp \
        ServerConnection.cpp \
        ThreadInfo.cpp

LOCAL_ADDITIONAL_DEPENDENCIES := \
	$(call module-built-files, libut_rendercontrol_enc) \
	$(call module-built-files, libGLESv1_enc)


emulatorOpengl := $(LOCAL_PATH)/../..

LOCAL_C_INCLUDES := $(emulatorOpengl)/system/OpenglCodecCommon \
		$(call intermediates-dir-for, SHARED_LIBRARIES, libut_rendercontrol_enc) \
		$(call intermediates-dir-for, SHARED_LIBRARIES, libGLESv1_enc) \
        $(emulatorOpengl)/system/GLESv1_enc \
        $(emulatorOpengl)/tests/ut_rendercontrol_enc 


LOCAL_CFLAGS := -DLOG_TAG=\"eglWrapper\"
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/egl
LOCAL_MODULE := libGLES_emul
LOCAL_MODULE_TAGS := debug
LOCAL_PRELINK_MODULE := false

#LOCAL_LDLIBS := -lpthread -ldl
LOCAL_SHARED_LIBRARIES := libdl libcutils libGLESv1_enc libut_rendercontrol_enc
LOCAL_STATIC_LIBRARIES := libOpenglCodecCommon

include $(BUILD_SHARED_LIBRARY)

#### egl.cfg ####
include $(CLEAR_VARS)

LOCAL_MODULE := egl.cfg
LOCAL_SRC_FILES := $(LOCAL_MODULE)

LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/egl
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := ETC

include $(BUILD_PREBUILT)

#### gles_emul.cfg ####
include $(CLEAR_VARS)

LOCAL_MODULE := gles_emul.cfg
LOCAL_SRC_FILES := $(LOCAL_MODULE)

LOCAL_MODULE_PATH := $(TARGET_OUT)/etc
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := ETC

include $(BUILD_PREBUILT)




