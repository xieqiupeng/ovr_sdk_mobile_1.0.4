LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# libvrsound.a
#
# VrSound
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := vrsound			# generate libvrsound.a

LOCAL_SRC_FILES := ../../../Libs/Android/$(TARGET_ARCH_ABI)/$(BUILDTYPE)/libvrsound.a

LOCAL_EXPORT_C_INCLUDES := \
  $(LOCAL_PATH)/../../../../LibOVRKernel/Src \
  $(LOCAL_PATH)/../../../../VrAppFramework/Include \
  $(LOCAL_PATH)/../../../Include

LOCAL_STATIC_LIBRARIES += vrappframework libovrkernel

# NOTE: This check is added to prevent the following error when running a "make clean" where
# the prebuilt lib may have been deleted: "LOCAL_SRC_FILES points to a missing file"
ifneq (,$(wildcard $(LOCAL_PATH)/$(LOCAL_SRC_FILES)))
include $(PREBUILT_STATIC_LIBRARY)
endif

# Note: Even though we depend on LibOVRKernel, we don't explicitly import it
# since our dependent projects may want either a prebuilt or from-source version.

# Note: Even though we depend on VrAppFramework, we don't explicitly import it
# since our dependent projects may want either a prebuilt or from-source version.