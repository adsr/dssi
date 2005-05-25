## Process this file with automake to produce Makefile.in

SUBDIRS = jack-dssi-host examples tests

EXTRA_DIST = \
	autogen.sh \
	scripts/tableofcontents.pl \
	scripts/txt2html.pl \
	doc/RFC.txt \
	doc/TODO \
	doc/midi-controllers.txt \
	doc/why-use.txt \
	doc/RFC.html \
	doc/why-use.html

AUTOMAKE_OPTIONS = foreign

include_HEADERS = dssi/dssi.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dssi.pc

doc/%.html:	doc/%.txt
	perl ./scripts/txt2html.pl $< | perl ./scripts/tableofcontents.pl > $@

MAINTAINERCLEANFILES = doc/RFC.html doc/why-use.html

