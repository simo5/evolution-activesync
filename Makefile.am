ACLOCAL_AMFLAGS = -I m4 -I m4-repo
#SUBDIRS = libeasmail/tests

if ENABLE_MEEGO_UI
  MEEGO_DIR = meego
endif

if ENABLE_QT_CONFIG_TOOL
  QTCONFIG_DIR = QtActivesyncdConfig
endif

if ENABLE_CAMEL_BACKEND
  CAMEL_DIRS = libevoeas camel
endif

gsettings_SCHEMAS = org.meego.activesyncd.gschema.xml org.meego.activesyncd.account.gschema.xml

@GSETTINGS_RULES@

# full set of directories for "make dist"
DIST_SUBDIRS = . libeasaccount/src libeasclient libeasaccount/tests eas-daemon check_tests libevoeas camel meego QtActivesyncdConfig po collection configuration
# subset of that list for "make && make install"
SUBDIRS = . libeasaccount/src libeasclient eas-daemon check_tests $(CAMEL_DIRS) $(MEEGO_DIR) $(QTCONFIG_DIR) po collection configuration

dist_noinst_DATA = $(gsettings_SCHEMAS)

EXTRA_DIST = autogen.sh

pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = libeasaccount.pc libeasclient.pc

# make [s]rpms
RPMBUILD ?= $(PWD)/rpmbuild

rpmroot:
	$(MKDIR_P) $(RPMBUILD)/BUILD
	$(MKDIR_P) $(RPMBUILD)/RPMS
	$(MKDIR_P) $(RPMBUILD)/SOURCES
	$(MKDIR_P) $(RPMBUILD)/SPECS
	$(MKDIR_P) $(RPMBUILD)/SRPMS

rpmbrprep: dist-xz rpmroot
	cp $(builddir)/contrib/evolution-activesync.spec $(RPMBUILD)/SPECS
	cp $(distdir).tar.xz $(RPMBUILD)/SOURCES

rpms: rpmbrprep
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" -ba SPECS/evolution-activesync.spec

srpm: rpmbrprep
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" -bs SPECS/evolution-activesync.spec
