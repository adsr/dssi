
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
	cp dssi/dssi.h $(PREFIX)/include/
	(cd examples && make install)
