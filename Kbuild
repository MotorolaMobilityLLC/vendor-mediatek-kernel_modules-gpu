KO_CODE_PATH := $(if $(filter /%,$(src)),,$(srctree)/)$(src)

ifneq ($(wildcard $(KO_CODE_PATH)/mt6991),)
        obj-m += mt6991/
endif

ifneq ($(wildcard $(KO_CODE_PATH)/mt6993),)
        obj-m += mt6993/
endif
