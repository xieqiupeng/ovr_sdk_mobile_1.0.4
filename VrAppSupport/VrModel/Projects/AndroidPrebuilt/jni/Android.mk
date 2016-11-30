LOCAL_PATH := $(call my-dir)

#--------------------------------------------------------
# libvrmodel.a
#
# VrModel
#--------------------------------------------------------
include $(CLEAR_VARS)				# clean everything up to prepare for a module

LOCAL_MODULE    := vrmodel			# generate libvrmodel.a

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../../../Src

LOCAL_SRC_FILES := ../../../Libs/Android/$(TARGET_ARCH_ABI)/$(BUILDTYPE)/libvrmodel.a

# NOTE: This check is added to prevent the following error when running a "make clean" where
# the prebuilt lib may have been deleted: "LOCAL_SRC_FILES points to a missing file"
ifneq (,$(wildcard $(LOCAL_PATH)/$(LOCAL_SRC_FILES)))
include $(PREBUILT_STATIC_LIBRARY)
endif
