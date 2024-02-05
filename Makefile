# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

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

CPATH := $(KSRC)/certs

MOD_DIR ?= updates

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

modules_install_only:
	$(MAKE) $(COMMON_ARGS) modules_install INSTALL_MOD_DIR=$(MOD_DIR)
	mkdir -p $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)-symvers
	cp -f ./src/Module.symvers \
		$(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)-symvers/nfp_driver.symvers

install: build
ifeq ("$(wildcard /lib/modules/$(KVER)/build/System.map)","")
	ln -sf /boot/System.map-$(KVER) /lib/modules/$(KVER)/build/System.map
endif
ifeq ("$(wildcard $(CPATH))","")
	$(shell mkdir -p $(CPATH))
endif
ifeq ("$(wildcard $(CPATH)/signing_key.pem)","")
ifneq (, $(shell which openssl))
	openssl req -new -x509 -newkey rsa:2048 -keyout $(CPATH)/signing_key.pem -outform DER -out $(CPATH)/signing_key.x509 -nodes -subj '/CN=localhost'
else
	$(warning OpenSSL not installed. Kernel module will not be signed.)
endif
endif
ifeq ("$(wildcard tools)", "tools")
	install -d "/opt/netronome/drv"

	# set_irq_affinity, install set_irq_affinity tool
	install -m 755 tools/set_irq_affinity.sh /opt/netronome/drv/nfp_set_irq_affinity

	# nfp_troubleshoot, install nfp_troubleshoot_gather script
	install -m 755 tools/nfp_troubleshoot_gather.py /opt/netronome/drv/nfp_troubleshoot_gather

	# profile, install script to add /opt/netronome/drv to the PATH
	install -d "/etc/profile.d"
	install -m 755 tools/profile.sh /etc/profile.d/nfp_drv_kmods_dkms_profile.sh
endif
	$(MAKE) modules_install_only

uninstall:
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)/nfp.ko
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)/nfp_net.ko
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)/nfp_netvf.ko
	rm -f $(INSTALL_MOD_PATH)/lib/modules/$(KVER)/$(MOD_DIR)-symvers/nfp_driver.symvers
	rm -rf /opt/netronome/drv
	rm -f /etc/profile.d/nfp_drv_kmods_dkms_profile.sh
	depmod $(DEPMOD_PATH) $(KVER)

.PHONY: build nfp_net noisy coccicheck sparse clean modules_install_only install uninstall
