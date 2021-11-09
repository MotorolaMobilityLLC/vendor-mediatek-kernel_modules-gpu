define build_kernel_modules
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/gpu/arm/midgard modules $(KBUILD_OPTIONS) MTK_PLATFORM_VERSION=$(1) LOCAL_MTK_GPU_VERSION=$(4) BUILD_RULE=$(RULE)
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/memory_group_manager modules $(KBUILD_OPTIONS) MTK_PLATFORM_VERSION=$(1) CONFIG_MALI_MEMORY_GROUP_MANAGER=$(MEMORY_GROUP_MANAGER)
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/protected_memory_allocator modules $(KBUILD_OPTIONS) MTK_PLATFORM_VERSION=$(1) CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR=$(PROTECTED_MEMORY_ALLOCATOR)
endef

define install_kernel_modules
$(MAKE) M=$(M)/$(1)/$(2)/$(3)/drivers/gpu/arm/midgard modules -C $(KERNEL_SRC) modules_install $(KBUILD_OPTIONS) MTK_PLATFORM_VERSION=$(1) LOCAL_MTK_GPU_VERSION=$(4) BUILD_RULE=$(RULE)
$(MAKE) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/memory_group_manager modules -C $(KERNEL_SRC) modules_install $(KBUILD_OPTIONS) CONFIG_MALI_MEMORY_GROUP_MANAGER=$(MEMORY_GROUP_MANAGER)
$(MAKE) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/protected_memory_allocator -C $(KERNEL_SRC) modules_install $(KBUILD_OPTIONS) CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR=$(PROTECTED_MEMORY_ALLOCATOR)
endef

define clean_kernel_modules
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/gpu/arm/midgard modules -C $(KERNEL_SRC) clean $(KBUILD_OPTIONS) MTK_PLATFORM_VERSION=$(1) LOCAL_MTK_GPU_VERSION=$(4) BUILD_RULE=$(RULE)
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/memory_group_manager modules clean $(KBUILD_OPTIONS) CONFIG_MALI_MEMORY_GROUP_MANAGER=$(MEMORY_GROUP_MANAGER)
$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(1)/$(2)/$(3)/drivers/base/arm/protected_memory_allocator clean $(KBUILD_OPTIONS) CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR=$(PROTECTED_MEMORY_ALLOCATOR)
endef

BUILD_RULE := OOT
CONFIG_MALI_MEMORY_GROUP_MANAGER := y
CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR := y

all: RULE := $(BUILD_RULE)
all: MEMORY_GROUP_MANAGER := $(CONFIG_MALI_MEMORY_GROUP_MANAGER)
all: PROTECTED_MEMORY_ALLOCATOR := $(CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR)
all:
ifneq (,$(wildcard mt6893))
	$(call build_kernel_modules,mt6893,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6893)
endif
ifneq (,$(wildcard mt6983))
	$(call build_kernel_modules,mt6983,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6983)
endif
ifneq (,$(wildcard mt6895))
	$(call build_kernel_modules,mt6895,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6895)
endif
ifneq (,$(wildcard mt6879))
	$(call build_kernel_modules,mt6879,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6879)
endif

modules_install: RULE := $(BUILD_RULE)
modules_install: MEMORY_GROUP_MANAGER := $(CONFIG_MALI_MEMORY_GROUP_MANAGER)
modules_install: PROTECTED_MEMORY_ALLOCATOR := $(CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR)
modules_install:
ifneq (,$(wildcard mt6893))
	$(call install_kernel_modules,mt6893,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6893)
endif
ifneq (,$(wildcard mt6983))
	$(call install_kernel_modules,mt6983,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6983)
endif
ifneq (,$(wildcard mt6895))
	$(call install_kernel_modules,mt6895,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6895)
endif
ifneq (,$(wildcard mt6879))
	$(call install_kernel_modules,mt6879,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6879)
endif

clean: RULE := $(BUILD_RULE)
clean: MEMORY_GROUP_MANAGER := $(CONFIG_MALI_MEMORY_GROUP_MANAGER)
clean: PROTECTED_MEMORY_ALLOCATOR := $(CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR)
clean:
ifneq (,$(wildcard mt6893))
	$(call clean_kernel_modules,mt6893,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6893)
endif
ifneq (,$(wildcard mt6983))
	$(call clean_kernel_modules,mt6983,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6983)
endif
ifneq (,$(wildcard mt6895))
	$(call clean_kernel_modules,mt6895,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6895)
endif
ifneq (,$(wildcard mt6879))
	$(call clean_kernel_modules,mt6879,mali_valhall,mali-r32p1,mali_valhall_r32p1_mt6879)
endif
