CFLAGS = -Wall -Werror -I../dssi

all: controller

test: all
	./controller

controller.o: ../dssi/dssi.h

clean:
	rm -f *.o

distclean:	clean
	rm -f *~ controller

