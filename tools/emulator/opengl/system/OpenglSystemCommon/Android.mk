LOCAL_PATH := $(call my-dir)

$(call emugl-begin-shared-library,libOpenglSystemCommon)
$(call emugl-import,libGLESv1_enc libGLESv2_enc lib_renderControl_enc)

LOCAL_SRC_FILES := \
    HostConnection.cpp \
    QemuPipeStream.cpp \
    Gem5PipeStream.cpp \
    m5/m5op_arm.S \
    ThreadInfo.cpp

$(call emugl-export,C_INCLUDES,$(LOCAL_PATH) bionic/libc/private)

$(call emugl-end-module)
