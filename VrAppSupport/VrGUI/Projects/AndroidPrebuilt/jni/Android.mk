LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# libvrgui.a
#
# VrGUI
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := vrgui			# generate libvrgui.a

LOCAL_SRC_FILES := ../../../Libs/Android/$(TARGET_ARCH_ABI)/$(BUILDTYPE)/libvrgui.a

LOCAL_EXPORT_C_INCLUDES := \
  $(LOCAL_PATH)/../../../../VrAppFramework/Include \
  $(LOCAL_PATH)/../../../Src

LOCAL_STATIC_LIBRARIES += vrappframework

# NOTE: This check is added to prevent the following error when running a "make clean" where
# the prebuilt lib may have been deleted: "LOCAL_SRC_FILES points to a missing file"
ifneq (,$(wildcard $(LOCAL_PATH)/$(LOCAL_SRC_FILES)))
include $(PREBUILT_STATIC_LIBRARY)
endif

# Note: Even though we depend on VrAppFramework, we don't explicitly import it
# since our dependent projects may want either a prebuilt or from-source version.
