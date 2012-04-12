cc=gcc
CC=g++
ld=ld

all: demo test stdiowrap.o stdiowrap++.o stdiowrapmock++.o

## objects

filebufwrap.o: filebufwrap.cpp fstreamwrap.h stdiowrap.h Makefile
	${CC} -Wall -O2 -c filebufwrap.cpp -DBACKEND_SHM

fstreamwrap.o: fstreamwrap.cpp fstreamwrap.h filebufwrap.o Makefile
	${CC} -Wall -O2 -c fstreamwrap.cpp -DBACKEND_SHM

streambufwrap.o: streambufwrap.cpp streambufwrap.h stdiowrap.h Makefile
	${CC} -Wall -O2 -c streambufwrap.cpp -DBACKEND_SHM

## user libraries

stdiowrap.o: stdiowrap.c stdiowrap.h Makefile
	${cc} -Wall -O2 -c stdiowrap.c -DBACKEND_SHM

stdiowrap_mock.o: stdiowrap_mock.c stdiowrap.h Makefile
	${cc} -Wall -O2 -c stdiowrap_mock.c -DBACKEND_SHM

stdiowrap++.o: stdiowrap.o filebufwrap.o fstreamwrap.o Makefile
	${ld} -r stdiowrap.o filebufwrap.o fstreamwrap.o -o stdiowrap++.o

stdiowrapmock++.o: stdiowrap_mock.o filebufwrap.o fstreamwrap.o Makefile
	${ld} -r stdiowrap_mock.o filebufwrap.o fstreamwrap.o -o stdiowrapmock++.o

## demostrations

test.o: test.c stdiowrap.h Makefile
	${cc} -Wall -O2 -c test.c

test: test.o stdiowrap.o Makefile
	${cc} -Wall -O2 -o test test.o stdiowrap.o

demo.o: demo.c stdiowrap.h streambufwrap.h Makefile
	${CC} -Wall -O2 -c demo.c

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
