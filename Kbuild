KO_CODE_PATH := $(if $(filter /%,$(src)),,$(srctree)/)$(src)

ifneq (,$(filter $(CONFIG_MTK_GPU_MT6768_SUPPORT),y m))
ifneq ($(wildcard $(KO_CODE_PATH)/mt6768),)
        obj-m += mt6768/
endif
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6991),)
        obj-m += mt6991/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6993),)
        obj-m += mt6993/
endif
