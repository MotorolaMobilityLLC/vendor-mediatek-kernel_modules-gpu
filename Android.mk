LOCAL_PATH := $(call my-dir)
LOCAL_PATH_B := $(LOCAL_PATH)

LOCAL_MTK_GPU_VERSION := mali_valhall_r30p0_mt6893
LOCAL_MTK_PLATFORM := $(word 4,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))
include $(LOCAL_PATH_B)/gpu_$(word 1,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali_$(word 2,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali-$(word 3,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/drivers/gpu/arm/midgard/Android.mk

LOCAL_MTK_GPU_VERSION := mali_valhall_r30p0_mt6983
LOCAL_MTK_PLATFORM := $(word 4,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))
include $(LOCAL_PATH_B)/gpu_$(word 1,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali_$(word 2,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali-$(word 3,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/drivers/gpu/arm/midgard/Android.mk

LOCAL_MTK_GPU_VERSION := mali_valhall_r30p0_mt6879
LOCAL_MTK_PLATFORM := $(word 4,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))
include $(LOCAL_PATH_B)/gpu_$(word 1,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali_$(word 2,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/mali-$(word 3,$(subst _, ,$(LOCAL_MTK_GPU_VERSION)))/drivers/gpu/arm/midgard/Android.mk
