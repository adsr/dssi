
PREFIX	:= /usr/local

all:
	(cd examples && make)
	(cd tests && make test)

clean:
	(cd examples && make clean)
	(cd tests && make clean)

distclean:
	(cd examples && make distclean)
	(cd tests && make distclean)
	rm -f *~

install: all
	mkdir -p $(PREFIX)/include
	mkdir -p $(PREFIX)/lib/pkgconfig
	cp dssi/dssi.h $(PREFIX)/include/
	cp dssi.pc $(PREFIX)/lib/pkgconfig/
	(cd examples && make install)
