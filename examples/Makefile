
PREFIX ?= /usr/local

LIBLO_CFLAGS ?= $(shell pkg-config liblo --cflags)
LIBLO_LIBS ?= $(shell pkg-config liblo --libs || echo -llo -lpthread)

QTDIR ?= /usr/lib/qt3
QTINCDIR ?= $(QTDIR)/include
QTLIBDIR ?= $(QTDIR)/lib
MOC ?= $(QTDIR)/bin/moc

TARGETS = \
	trivial_synth.so \
	less_trivial_synth.so \
	trivial_sampler.so \
	dssi_osc_send \
	dssi_osc_update \
	less_trivial_synth_qt_gui \
	less_trivial_synth/LTS_qt \
	trivial_sampler_qt_gui \
	trivial_sampler/trivial_sampler_qt

all: $(TARGETS)

install: all
	mkdir -p $(PREFIX)/bin
	cp dssi_osc_update dssi_osc_send $(PREFIX)/bin/
	mkdir -p $(PREFIX)/lib/dssi
	cp trivial_synth.so $(PREFIX)/lib/dssi/
	cp trivial_sampler.so $(PREFIX)/lib/dssi/
	mkdir -p $(PREFIX)/lib/dssi/less_trivial_synth
	cp less_trivial_synth.so $(PREFIX)/lib/dssi/
	cp less_trivial_synth_qt_gui $(PREFIX)/lib/dssi/less_trivial_synth/LTS_qt
	mkdir -p $(PREFIX)/lib/dssi/trivial_sampler
	cp trivial_sampler.so $(PREFIX)/lib/dssi/
	cp trivial_sampler_qt_gui $(PREFIX)/lib/dssi/trivial_sampler/trivial_sampler_qt


CFLAGS = -Wall -g3 -I../dssi $(LIBLO_CFLAGS)
CXXFLAGS = $(CFLAGS) -I$(QTINCDIR)
LDLIBS = $(LIBLO_LIBS)

QTGUI_LDLIBS = -L$(QTDIR)/lib -lqt-mt $(LIBLO_LIBS)

MB_O = ../message_buffer/message_buffer.o

dssi_osc_send:	dssi_osc_send.o

dssi_osc_update:	dssi_osc_update.o

less_trivial_synth_qt_gui.o:	less_trivial_synth_qt_gui.h

less_trivial_synth_qt_gui.moc.cpp:	less_trivial_synth_qt_gui.h
	$(MOC) $< > $@

less_trivial_synth_qt_gui: less_trivial_synth_qt_gui.o less_trivial_synth_qt_gui.moc.o
	$(CXX) -o $@ $^ $(QTGUI_LDLIBS)

less_trivial_synth/LTS_qt: less_trivial_synth_qt_gui
	mkdir -p less_trivial_synth
	cp less_trivial_synth_qt_gui less_trivial_synth/LTS_qt

trivial_sampler_qt_gui.o:	trivial_sampler_qt_gui.h

trivial_sampler_qt_gui.moc.cpp:	trivial_sampler_qt_gui.h
	$(MOC) $< > $@

trivial_sampler_qt_gui: trivial_sampler_qt_gui.o trivial_sampler_qt_gui.moc.o
	$(CXX) -o $@ $^ -lsndfile $(QTGUI_LDLIBS)

trivial_sampler/trivial_sampler_qt: trivial_sampler_qt_gui
	mkdir -p trivial_sampler
	cp trivial_sampler_qt_gui trivial_sampler/trivial_sampler_qt

%.so: %.o ../dssi/dssi.h $(MB_O)
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o $(MB_O)

trivial_sampler.so: trivial_sampler.o ../dssi/dssi.h
	gcc -nostartfiles -shared -lsndfile -lsamplerate -lc -lm -o trivial_sampler.so trivial_sampler.o

clean:
	rm -f *.o *.moc.*

distclean:	clean
	rm -f *~ $(TARGETS)
	rm -rf less_trivial_synth
	rm -rf trivial_sampler
