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

