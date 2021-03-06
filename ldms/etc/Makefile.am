ACLOCAL_AMFLAGS = -I m4
SUBDIRS =

# init script
#   init.d is currently unmaintained
# SUBDIRS += init.d

# logrotate
#   logrotate.d is currently unmaintained
# SUBDIRS += logrotate.d

# systemd config files
SUBDIRS += systemd

do_subst = @OVIS_SUBST_RULE@

EXTRA_DIST = ldmsauth.conf \
	     sampler.conf \
	     papi-sampler.conf \
	     kokkos.conf \
	     sampler.conf.cray_xc.example \
	     aggregator.conf \
	     aggregator.conf.cray_xc.example \
	     ldmsd.aggregator.env.in \
	     ldmsd.aggregator.env.cray_xc.example \
	     ldmsd.sampler.env.in \
	     ldmsd.sampler.env.cray_xc.example \
	     ldmsd.kokkos.env.in \
	     papi-sampler.env.in \
	     ldms.plugstack.env.in \
	     ldms.cfg

ENV_FILES = $(builddir)/ldmsd.sampler.env \
	    $(builddir)/ldmsd.aggregator.env \
	    $(builddir)/ldmsd.kokkos.env \
	    $(builddir)/papi-sampler.env \
	    $(builddir)/ldms.plugstack.env

EXAMPLE_FILES = $(srcdir)/aggregator.conf.cray_xc.example \
		$(srcdir)/sampler.conf.cray_xc.example \
		$(srcdir)/ldmsd.aggregator.env.cray_xc.example \
		$(srcdir)/ldmsd.sampler.env.cray_xc.example

all: $(ENV_FILES)

$(builddir)/%.env: $(srcdir)/%.env.in
	$(do_subst) < $< > $@

CONF_DIR = $(DESTDIR)$(sysconfdir)/ldms

# These are ldms-specific config
install-data-local:
	$(MKDIR_P) $(CONF_DIR)
	$(INSTALL_DATA) $(ENV_FILES) $(CONF_DIR)/
	$(INSTALL) -m 600 $(srcdir)/ldmsauth.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldms.cfg $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/sampler.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/papi-sampler.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/aggregator.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/kokkos.conf $(CONF_DIR)
	$(INSTALL_DATA) $(EXAMPLE_FILES) $(CONF_DIR)/

uninstall-local:
	rm -f $(CONF_DIR)/ldmsd.aggregator.env
	rm -f $(CONF_DIR)/ldmsd.sampler.env
	rm -f $(CONF_DIR)/ldmsd.kokkos.env
	rm -f $(CONF_DIR)/ldmsauth.conf
	rm -f $(CONF_DIR)/sampler.conf
	rm -f $(CONF_DIR)/papi-sampler.conf
	rm -f $(CONF_DIR)/aggregator.conf
	rm -f $(CONF_DIR)/kokkos.conf
	rm -f $(CONF_DIR)/ldms.cfg
	rm -f $(CONF_DIR)/ldms.plugstack.env
	rm -f $(CONF_DIR)/aggregator.conf.cray_xc.example
	rm -f $(CONF_DIR)/sampler.conf.cray_xc.example
	rm -f $(CONF_DIR)/ldmsd.aggregator.env.cray_xc.example
	rm -f $(CONF_DIR)/ldmsd.sampler.env.cray_xc.example
