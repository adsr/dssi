TARGETS = \
	fluid-dssi.so

all: $(TARGETS)

CFLAGS = -Wall -g3 -I../../dssi $(shell pkg-config liblo --cflags)
CXXFLAGS = $(CFLAGS) -I$(QTDIR)/include

LDLIBS = -lfluidsynth -ljack -lasound -lpthread $(shell pkg-config liblo --libs)

fluid-dssi.o:	fluid-dssi.h ../../dssi/dssi.h

%.so: %.o ../../dssi/dssi.h 
	g++ -shared -lc -lm -o $*.so $*.o $(LDLIBS)

clean:
	rm -f *.o *.so *.moc.*

distclean:	clean
	rm -f *~ $(TARGETS)
	rmdir fluid-dssi

