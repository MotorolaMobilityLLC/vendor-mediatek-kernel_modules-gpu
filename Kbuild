KO_CODE_PATH := $(if $(filter /%,$(src)),,$(srctree)/)$(src)
ifneq (,$(filter $(CONFIG_MTK_GPU_MT6768_SUPPORT),y m))
ifneq ($(wildcard $(KO_CODE_PATH)/mt6768),)
        obj-m += mt6768/
endif
else ifeq ($(CONFIG_MTK_GPU_MT6877_SUPPORT),m)
ifneq ($(wildcard $(KO_CODE_PATH)/mt6877),)
        obj-m += mt6877/
endif
else
ifeq ($(CONFIG_MTK_GPU_MT6893_SUPPORT),m)
ifneq ($(wildcard $(KO_CODE_PATH)/mt6893),)
	obj-m += mt6893/
endif
else
ifneq ($(wildcard $(KO_CODE_PATH)/mt6878),)
	obj-m += mt6878/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6897),)
	obj-m += mt6897/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6985),)
	obj-m += mt6985/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6989),)
        obj-m += mt6989/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6991),)
        obj-m += mt6991/
endif

endif

endif
