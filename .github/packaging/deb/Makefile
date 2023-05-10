#/usr/bin/make
SRC = $(DESTDIR)/usr/src
SHARE = $(DESTDIR)/usr/share/$(NAME)-dkms

all:

clean:

install:

# source tree
ifeq ("$(wildcard $(NAME)-$(VERSION))", "$(NAME)-$(VERSION)")
	install -d "$(SRC)"
	cp -a $(NAME)-$(VERSION) $(SRC)
	chmod 644 -R "$(SRC)/$(NAME)-$(VERSION)"
endif

# tarball, possibly with binaries
ifeq ("$(wildcard $(NAME)-$(VERSION).dkms.tar.gz)", "$(NAME)-$(VERSION).dkms.tar.gz")
	install -d "$(SHARE)"
	install -m 644 $(NAME)-$(VERSION).dkms.tar.gz "$(SHARE)"
endif

# tools, install driver tools if present
ifeq ("$(wildcard tools)", "tools")
	install -d "$(DESTDIR)/opt/netronome/drv"

	# set_irq_affinity, install set_irq_affinity tool
	install -m 755 tools/set_irq_affinity.sh $(DESTDIR)/opt/netronome/drv/nfp_set_irq_affinity

	# profile, install script to add /opt/netronome/drv to the PATH
	install -m 644 -d "$(DESTDIR)/etc/profile.d"
	install -m 644 tools/profile.sh $(DESTDIR)/etc/profile.d/nfp_drv_kmods_dkms_profile.sh
endif

# postinst, only if we are supporting legacy mode
ifeq ("$(wildcard common.postinst)", "common.postinst")
	install -d "$(SHARE)"
	install -m 755 common.postinst $(SHARE)/postinst
endif
