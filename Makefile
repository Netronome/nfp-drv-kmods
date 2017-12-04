#####################################
# Work out where the kernel source is
ifeq (,$(KVER))
  KVER=$(shell uname -r)
endif

KERNEL_SEARCH_PATH := \
	/lib/modules/$(KVER)/build \
	/lib/modules/$(KVER)/source \
	/usr/src/linux-$(KVER) \
	/usr/src/linux-$($(KVER) | sed 's/-.*//') \
	/usr/src/kernel-headers-$(KVER) \
	/usr/src/kernel-source-$(KVER) \
	/usr/src/linux-$($(KVER) | sed 's/\([0-9]*\.[0-9]*\)\..*/\1/') \
	/usr/src/linux

# prune list to those containing a configured kernel source tree
test_dir = $(shell [ -e $(dir)/include/config ] && echo $(dir))
KERNEL_SEARCH_PATH := $(foreach dir, $(KERNEL_SEARCH_PATH), $(test_dir))
ifneq (,$(INSTALL_MOD_PATH))
  DEPMOD_PATH=--basedir=$(INSTALL_MOD_PATH)
endif

# Use first one
ifeq (,$(KSRC))
  KSRC := $(firstword $(KERNEL_SEARCH_PATH))
endif

ifeq (,$(KSRC))
  $(error Could not find kernel source)
endif

EXTRA_CFLAGS += $(CFLAGS_EXTRA)

###########################################################################
# Build rules

COMMON_ARGS := -C $(KSRC) M=`pwd`/src

build:
	$(MAKE) $(COMMON_ARGS) modules

nfp_net:
	$(MAKE) $(COMMON_ARGS) CONFIG_NFP_EXPORTS=n CONFIG_NFP_USER_SPACE_CPP=n modules

noisy: clean
	$(MAKE) $(COMMON_ARGS) V=1 modules

coccicheck: clean
	$(MAKE) $(COMMON_ARGS) coccicheck MODE=report

sparse: clean
	 $(MAKE) $(COMMON_ARGS) C=2 CF="-D__CHECK_ENDIAN__ -Wbitwise -Wcontext" modules

clean:
	$(MAKE) -C $(KSRC) M=`pwd` clean

install: build
	$(MAKE) $(COMMON_ARGS) modules_install

uninstall:
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/extra/nfp.ko
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/extra/nfp_net.ko
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/extra/nfp_netvf.ko
	depmod $(DEPMOD_PATH) $(KVER)

.PHONY: build nfp_net noisy coccicheck sparse clean install uninstall
