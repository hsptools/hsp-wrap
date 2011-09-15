############################################################


# optimized build (default)
#CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
CC= mpicc
OPTS=   
CFLAGS= -O3 -Wall
LIBS=   -lm -lz -lexpat

# debug build
#DBG_CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
DBG_CC= mpicc
DBG_OPTS=   
DBG_CFLAGS= -g -Wall
DBG_LIBS=   -lm -lz -lexpat

# profile options
#PROF_CC= /usr/mpi/gcc/openmpi-1.4.2-qlc/bin/mpicc
PROF_CC= mpicc
PROF_OPTS=
PROF_CFLAGS= -O3 -Wall
PROF_LIBS=   -lm -lz -lexpat $(FPMPI_LDFLAGS)


############################################################


# default build
all: mcw wrap zextract tools

tools: SHMclean xmlparse fastatools mol2tools
fastatools: fastalens fastadist splitfasta sortfasta compressfasta
mol2tools: splitmol2 compressmol2

# debug build
debug: CC     = $(DBG_CC)
debug: OPTS   = $(DBG_OPTS)
debug: CFLAGS = $(DBG_CFLAGS)
debug: LIBS   = $(DBG_LIBS)
debug: clean all

# profile build
profile: CC     = $(PROF_CC)
profile: OPTS   = $(PROF_OPTS)
profile: CFLAGS = $(PROF_CFLAGS)
profile: LIBS   = $(PROF_LIBS)
profile: clean all


############################################################


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
	$(CC) $(OPTS) $(CFLAGS) -c compressfasta.c

compressfasta: compressfasta.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o compressfasta compressfasta.o $(LIBS)

##

compressmol2.o: compressmol2.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c compressmol2.c

compressmol2: compressmol2.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o compressmol2 compressmol2.o $(LIBS)

##

SHMclean.o: SHMclean.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c SHMclean.c

SHMclean: SHMclean.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o SHMclean SHMclean.o $(LIBS)

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

zextract: zextract.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o zextract zextract.o $(LIBS)

##

wrap.o: wrap.c Makefile
	$(CC) $(OPTS) $(CFLAGS) -c wrap.c

wrap: wrap.o Makefile
	$(CC) $(OPTS) $(CFLAGS) -o wrap wrap.o $(LIBS)


############################################################

# Maintenance
clean:
	rm -f *.o mcw sortfasta SHMclean zextract wrap xmlparse fastalens fastadist compressmol2 splitmol2 splitfasta

strip: clean
	rm -f *~ \#*
