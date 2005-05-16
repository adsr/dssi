
PREFIX ?= /usr/local
PKGCONFIG_INSTALL_DIR = $(PREFIX)/lib/pkgconfig

export PREFIX

all:	doc/RFC.html doc/why-use.html
	@$(MAKE) -C jack-dssi-host
	@$(MAKE) -C examples
	@$(MAKE) -C tests

clean:
	@$(MAKE) -C jack-dssi-host clean
	@$(MAKE) -C examples clean
	@$(MAKE) -C tests clean

distclean:
	@$(MAKE) -C jack-dssi-host distclean
	@$(MAKE) -C examples distclean
	@$(MAKE) -C tests distclean
	rm -f *~ doc/*~ dssi/*~

install: all
	mkdir -p $(PREFIX)/include
	mkdir -p $(PKGCONFIG_INSTALL_DIR)
	cp dssi/dssi.h $(PREFIX)/include/
	sed s:.PREFIX.:$(PREFIX): dssi.pc >$(PKGCONFIG_INSTALL_DIR)/dssi.pc
	@$(MAKE) -C jack-dssi-host install
	@$(MAKE) -C examples install

doc/%.html:	doc/%.txt
	perl ./scripts/txt2html.pl $< | perl ./scripts/tableofcontents.pl > $@
