SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system

do_subst = @OVIS_SUBST_RULE@

SVC_FILES = $(builddir)/ldmsd.sampler.service \
	    $(builddir)/ldmsd.aggregator.service \
	    $(builddir)/ldmsd.kokkos.service \
	    $(builddir)/papi-sampler.service

all: $(SVC_FILES)

$(builddir)/%.service: $(srcdir)/%.service.in
	$(do_subst) < $< > $@

EXTRA_DIST = ldmsd.aggregator.service.in \
	     ldmsd.sampler.service.in \
	     ldmsd.kokkos.service.in \
	     papi-sampler.service.in

install-data-local:
	$(MKDIR_P) $(SYSTEMD_DIR)
	$(INSTALL_DATA) $(SVC_FILES) $(SYSTEMD_DIR)/

uninstall-local:
	rm -f $(SYSTEMD_DIR)/ldmsd.sampler.service
	rm -f $(SYSTEMD_DIR)/ldmsd.aggregator.service
	rm -f $(SYSTEMD_DIR)/ldmsd.kokkos.service
	rm -f $(SYSTEMD_DIR)/papi-sampler.service
