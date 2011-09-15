cc=gcc
CC=g++

all: demo test stdiowrap.o stdstreamwrap.o #papiwrap.o papi_sum

stdstreamwrap.o: stdstreamwrap.cpp stdstreamwrap.h stdiowrap.h Makefile
	${CC} -Wall -O2 -c stdstreamwrap.cpp -DBACKEND_SHM

stdiowrap.o: stdiowrap.c stdiowrap.h Makefile
	${cc} -Wall -O2 -c stdiowrap.c -DBACKEND_SHM

test.o: test.c stdiowrap.h Makefile
	${cc} -Wall -O2 -c test.c

test: test.o stdiowrap.o Makefile
	${cc} -Wall -O2 -o test test.o stdiowrap.o

demo.o: demo.cpp stdstreamwrap.h stdiowrap.h Makefile
	${CC} -Wall -O2 -c demo.cpp

demo: demo.o stdiowrap.o stdstreamwrap.o Makefile
	${CC} -o demo demo.o stdiowrap.o stdstreamwrap.o

papi_sum: papi_sum.c Makefile
	${cc} -Wall -O2 -o papi_sum papi_sum.c -lm -lgmp

papiwrap.o: papiwrap.c papiwrap.h Makefile
	${cc} -Wall -O2 -c -o papiwrap.o papiwrap.c -lm -lpapi

clean:
	rm -f *.o test demo papi_sum

strip: clean
	rm -f *~
