
TARGETS = \
	dssi_example_host \
	trivial_synth.so \
	less_trivial_synth.so \
	dssi_osc_send \
	dssi_osc_update \
	less_trivial_synth_qt_gui \
	less_trivial_synth/LTS_qt

all: $(TARGETS)

CFLAGS = -Wall -g3 -I../dssi $(shell pkg-config liblo --cflags)
CXXFLAGS = $(CFLAGS) -I$(QTDIR)/include

LDLIBS = -ljack -lasound -lpthread $(shell pkg-config liblo --libs)
QTGUI_LDLIBS = -L$(QTDIR)/lib -lqt-mt $(shell pkg-config liblo --libs)

dssi_example_host:	dssi_example_host.o message_buffer.o

dssi_osc_send:	dssi_osc_send.o message_buffer.o

dssi_osc_update:	dssi_osc_update.o message_buffer.o

dssi_example_host.o:	../dssi/dssi.h dssi_example_host.h

less_trivial_synth_qt_gui.o:	less_trivial_synth_qt_gui.h

less_trivial_synth_qt_gui.moc.cpp:	less_trivial_synth_qt_gui.h
	moc $< > $@

less_trivial_synth_qt_gui: less_trivial_synth_qt_gui.o less_trivial_synth_qt_gui.moc.o
	$(CXX) -o $@ $^ $(QTGUI_LDLIBS)

less_trivial_synth/LTS_qt: less_trivial_synth_qt_gui
	mkdir -p less_trivial_synth
	cp less_trivial_synth_qt_gui less_trivial_synth/LTS_qt

%.so: %.o ../dssi/dssi.h message_buffer.o
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o message_buffer.o

clean:
	rm -f *.o *.so *.moc.*

distclean:	clean
	rm -f *~ $(TARGETS)

test: all
	DSSI_PATH=`pwd` ./dssi_example_host less_trivial_synth.so

