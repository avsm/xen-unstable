#
# Architecture special makerules for x86 family
# (including x86_32, x86_32y and x86_64).
#

ifeq ($(TARGET_ARCH),x86_32)
ARCH_CFLAGS  := -m32 -march=i686
ARCH_LDFLAGS := -m elf_i386
EXTRA_INC += $(TARGET_ARCH_FAM)/$(TARGET_ARCH)
EXTRA_SRC += arch/$(EXTRA_INC)
endif

ifeq ($(TARGET_ARCH)$(pae),x86_32y)
ARCH_CFLAGS  := -DCONFIG_X86_PAE=1
ARCH_ASFLAGS := -DCONFIG_X86_PAE=1
EXTRA_INC += $(TARGET_ARCH_FAM)/$(TARGET_ARCH)
EXTRA_SRC += arch/$(EXTRA_INC)
endif

ifeq ($(TARGET_ARCH),x86_64)
ARCH_CFLAGS := -m64 -mno-red-zone -fpic -fno-reorder-blocks
ARCH_CFLAGS := -fno-asynchronous-unwind-tables
ARCH_LDFLAGS := -m elf_x86_64
EXTRA_INC += $(TARGET_ARCH_FAM)/$(TARGET_ARCH)
EXTRA_SRC += arch/$(EXTRA_INC)
endif

