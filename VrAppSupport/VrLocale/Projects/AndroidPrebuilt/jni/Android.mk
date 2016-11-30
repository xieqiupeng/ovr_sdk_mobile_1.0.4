LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# libvrlocale.a
#
# VrLocale
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := vrlocale			# generate libvrlocale.a

LOCAL_SRC_FILES := ../../../Libs/Android/$(TARGET_ARCH_ABI)/$(BUILDTYPE)/libvrlocale.a

LOCAL_EXPORT_C_INCLUDES := \
  $(LOCAL_PATH)/../../../../LibOVRKernel/Src \
  $(LOCAL_PATH)/../../../Include

LOCAL_STATIC_LIBRARIES += libovrkernel

# NOTE: This check is added to prevent the following error when running a "make clean" where
# the prebuilt lib may have been deleted: "LOCAL_SRC_FILES points to a missing file"
ifneq (,$(wildcard $(LOCAL_PATH)/$(LOCAL_SRC_FILES)))
include $(PREBUILT_STATIC_LIBRARY)
endif

# Note: Even though we depend on LibOVRKernel, we don't explicitly import it
# since our dependent projects may want either a prebuilt or from-source version.
