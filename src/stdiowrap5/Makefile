CFLAGS=-Wall -ggdb
cc=gcc
CC=g++
ld=ld

all: demo test stdiowrap.o stdiowrap++.o 

## objects

filebufwrap.o: filebufwrap.cpp fstreamwrap.h stdiowrap.h Makefile
	${CC} ${CFLAGS} -c filebufwrap.cpp -DBACKEND_SHM

fstreamwrap.o: fstreamwrap.cpp fstreamwrap.h filebufwrap.o Makefile
	${CC} ${CFLAGS} -c fstreamwrap.cpp -DBACKEND_SHM

streambufwrap.o: streambufwrap.cpp streambufwrap.h stdiowrap.h Makefile
	${CC} ${CFLAGS} -c streambufwrap.cpp -DBACKEND_SHM

## user libraries

stdiowrap.o: stdiowrap.c stdiowrap.h Makefile
	${cc} ${CFLAGS} -c stdiowrap.c -DBACKEND_SHM

stdiowrap++.o: stdiowrap.o filebufwrap.o fstreamwrap.o Makefile
	${ld} -r stdiowrap.o filebufwrap.o fstreamwrap.o -o stdiowrap++.o

## demostrations

test.o: test.c stdiowrap.h Makefile
	${cc} ${CFLAGS} -c test.c

test: test.o stdiowrap.o Makefile
	${cc} ${CFLAGS} -o test test.o stdiowrap.o

demo.o: demo.cpp stdiowrap.h streambufwrap.h Makefile
	${CC} ${CFLAGS} -c demo.cpp

demo: demo.o stdiowrap.o streambufwrap.o Makefile
	${CC} -o demo demo.o stdiowrap.o streambufwrap.o

#papi_sum: papi_sum.c Makefile
#	${cc} -Wall -O2 -o papi_sum papi_sum.c -lm -lgmp
#
#papiwrap.o: papiwrap.c papiwrap.h Makefile
#	${cc} -Wall -O2 -c -o papiwrap.o papiwrap.c -lm -lpapi

clean:
	rm -f *.o test demo papi_sum

strip: clean
	rm -f *~
