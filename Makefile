
PREFIX ?= /usr/local
PKGCONFIG_INSTALL_DIR = $(PREFIX)/lib/pkgconfig

FLUID ?= $(PWD)/../fluidsynth-1.0.3

export PREFIX FLUID

all:	doc/RFC.html doc/why-use.html
	@$(MAKE) -C jack-dssi-host
	@$(MAKE) -C examples
	@$(MAKE) -C tests
	@test -d $(FLUID)/src && $(MAKE) -C fluidsynth-dssi || echo "WARNING: Fluidsynth sources not found in $(FLUID), not building fluidsynth-dssi"

clean:
	@$(MAKE) -C jack-dssi-host clean
	@$(MAKE) -C examples clean
	@$(MAKE) -C tests clean
	@$(MAKE) -C fluidsynth-dssi clean

distclean:
	@$(MAKE) -C jack-dssi-host distclean
	@$(MAKE) -C examples distclean
	@$(MAKE) -C tests distclean
	@$(MAKE) -C fluidsynth-dssi distclean
	rm -f *~ doc/*~ dssi/*~

install: all
	mkdir -p $(PREFIX)/include
	mkdir -p $(PKGCONFIG_INSTALL_DIR)
	cp dssi/dssi.h $(PREFIX)/include/
	cp dssi.pc $(PKGCONFIG_INSTALL_DIR)
	@$(MAKE) -C jack-dssi-host install
	@$(MAKE) -C examples install
	@test -d $(FLUID)/src && $(MAKE) -C fluidsynth-dssi install || echo "Not installing fluidsynth-dssi"

doc/%.html:	doc/%.txt
	perl ./scripts/txt2html.pl $< | perl ./scripts/tableofcontents.pl > $@
