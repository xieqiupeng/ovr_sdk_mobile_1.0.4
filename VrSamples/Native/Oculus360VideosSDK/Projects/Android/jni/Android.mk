LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := jpeg
LOCAL_SRC_FILES := libjpeg.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrsound
LOCAL_SRC_FILES := libvrsound.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrmodel
LOCAL_SRC_FILES := libvrmodel.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrlocale
LOCAL_SRC_FILES := libvrlocale.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrgui
LOCAL_SRC_FILES := libvrgui.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrappframework
LOCAL_SRC_FILES := libvrappframework.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := libovrkernel
LOCAL_SRC_FILES := libovrkernel.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := stb
LOCAL_SRC_FILES := libstb.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := openglloader
LOCAL_SRC_FILES := libopenglloader.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := minizip
LOCAL_SRC_FILES := libminizip.a

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module
LOCAL_ARM_MODE  := arm					# full speed arm instead of thumb
LOCAL_ARM_NEON  := true					# compile with neon support enabled
LOCAL_MODULE := vrapi
LOCAL_SRC_FILES := libvrapi.so

include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)					# clean everything up to prepare for a module

include ../../../../../cflags.mk

LOCAL_LDLIBS := -llog -landroid -lEGL

LOCAL_C_INCLUDES := \
  ../../../../../LibOVRKernel/Src \
  ../../../../../VrApi/Include \
  ../../../../../VrAppFramework/Include \
  ../../../../../VrAppSupport/VrGUI/Src \
  ../../../../../VrAppSupport/VrLocale/Include \
  ../../../../../VrAppSupport/VrModel/Src \
  ../../../../../VrAppSupport/VrSound/Include \
  ../../../../../1stParty/OpenGL_Loader/Include \
  ../../../../../3rdParty/minizip/src

LOCAL_MODULE    := oculus360videos		# generate oculus360videos.so
LOCAL_SRC_FILES  :=	../../../Src/Oculus360Videos.cpp \
					../../../Src/VideoBrowser.cpp \
					../../../Src/VideoMenu.cpp \
					../../../Src/VideosMetaData.cpp \
					../../../Src/OVR_TurboJpeg.cpp \
					../../../Src/crc32.c \
					../../../Src/inflate.c \
					../../../Src/inffast.c \
					../../../Src/zutil.c \
					../../../Src/adler32.c \
					../../../Src/inftrees.c

LOCAL_STATIC_LIBRARIES += vrsound vrmodel vrlocale vrgui vrappframework libovrkernel jpeg stb openglloader minizip
LOCAL_SHARED_LIBRARIES += vrapi

include $(BUILD_SHARED_LIBRARY)			# start building based on everything since CLEAR_VARS

# $(call import-module,3rdParty/stb/build/androidprebuilt/jni)
# $(call import-module,LibOVRKernel/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrApi/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrAppFramework/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrAppSupport/VrGUI/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrAppSupport/VrLocale/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrAppSupport/VrModel/Projects/AndroidPrebuilt/jni)
# $(call import-module,VrAppSupport/VrSound/Projects/AndroidPrebuilt/jni)