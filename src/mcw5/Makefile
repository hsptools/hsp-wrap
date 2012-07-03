############################################################


# optimized build (default)
#CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
CC= cc
CXX= CC
OPTS=   
CFLAGS= -O3 -Wall
LIBS=   -lm -lz -lexpat

# debug build
#DBG_CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
DBG_CC= cc
DBG_CXX= CC
DBG_OPTS=   
DBG_CFLAGS= -g -Wall
DBG_LIBS=   -lm -lz -lexpat

# profile options
#PROF_CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
PROF_CC= mpicc
PROF_CXX= CC
PROF_OPTS=
PROF_CFLAGS= -O3 -Wall
PROF_LIBS=   -lm -lz -lexpat $(FPMPI_LDFLAGS)


############################################################


# default build
all: mcw wrap zextract tools

tools: SHMclean xmlparse fastatools mol2tools
fastatools: fastalens fastadist splitfasta sortfasta compressfasta compressfasta0
mol2tools: splitmol2 compressmol2

# debug build
debug: CC     = $(DBG_CC)
debug: CXX    = $(DBG_CXX)
debug: OPTS   = $(DBG_OPTS)
debug: CFLAGS = $(DBG_CFLAGS)
debug: LIBS   = $(DBG_LIBS)
debug: clean all

# profile build
profile: CC     = $(PROF_CC)
profile: CXX    = $(PROF_CXX)
profile: OPTS   = $(PROF_OPTS)
profile: CFLAGS = $(PROF_CFLAGS)
profile: LIBS   = $(PROF_LIBS)
profile: clean all


############################################################

strutils.o: strutils.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c strutils.c

zutils.o: zutils.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c zutils.c

fastalens.o: fastalens.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c fastalens.c

fastalens: fastalens.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o fastalens fastalens.o $(LIBS)

##

fastadist.o: fastadist.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c fastadist.c

fastadist: fastadist.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o fastadist fastadist.o $(LIBS)

##

xmlparse.o: xmlparse.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c xmlparse.c

xmlparse: xmlparse.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o xmlparse xmlparse.o $(LIBS)

##

sortfasta.o: sortfasta.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c sortfasta.c

sortfasta: sortfasta.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o sortfasta sortfasta.o $(LIBS)

##

splitfasta.o: splitfasta.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c splitfasta.c

splitfasta: splitfasta.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o splitfasta splitfasta.o $(LIBS)

#

compressfasta.o: compressfasta.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -DCF_COMPRESSION_LEVEL=Z_DEFAULT_COMPRESSION -c compressfasta.c

compressfasta: compressfasta.o zutils.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o compressfasta compressfasta.o zutils.o $(LIBS)

#

compressfasta0.o: compressfasta.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -DCF_COMPRESSION_LEVEL=Z_NO_COMPRESSION -c compressfasta.c -o compressfasta0.o

compressfasta0: compressfasta0.o zutils.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o compressfasta0 compressfasta0.o zutils.o $(LIBS)

##

compressmol2.o: compressmol2.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c compressmol2.c

compressmol2: compressmol2.o zutils.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o compressmol2 compressmol2.o zutils.o $(LIBS)

##

SHMclean.o: SHMclean.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c SHMclean.c

SHMclean: SHMclean.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o SHMclean SHMclean.o $(LIBS)

##

recoverfasta: recoverfasta.cc Makefile
	$(CXX) $(OPTS) $(CFLAGS) $(LIBS) recoverfasta.cc -o recoverfasta

recovermol2: recovermol2.cc Makefile
	$(CXX) $(OPTS) $(CFLAGS) $(LIBS) recovermol2.cc -o recovermol2

##

tscq.o: tscq.c tscq.h Makefile
	$(CC) $(OPTS) $(CFLAGS) -c tscq.c

mcw.o: mcw.c mcw.h tscq.h Makefile
	$(CC) $(OPTS) $(CFLAGS) -c mcw.c

mcw: mcw.o tscq.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o mcw mcw.o tscq.o $(LIBS)

##

zextract.o: zextract.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c zextract.c

zextract: zextract.o zutils.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o zextract zextract.o zutils.o $(LIBS)

##

wrap.o: wrap.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c wrap.c

wrap: wrap.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o wrap wrap.o $(LIBS)


############################################################

# Maintenance
clean:
	rm -f *.o mcw sortfasta SHMclean zextract wrap xmlparse fastalens fastadist compressfasta compressfasta0 compressmol2 splitmol2 splitfasta recoverfasta recovermol2

strip: clean
	rm -f *~ \#*
