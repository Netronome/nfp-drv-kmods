######################################################
# Work out driver version if not build from repository
ifeq (,$(wildcard .git))
  NFPVER := $(shell cat .revision)
else
  NFPVER := 
endif

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

COMMON_ARGS := nfp_src_ver:=$(NFPVER) ccflags-y:="$(CFLAGS_EXTRA)" -C $(KSRC) M=`pwd`/src

build: clean
	$(MAKE) $(COMMON_ARGS) modules

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



