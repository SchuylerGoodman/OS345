IDIR=../include
CC=gcc
CFLAGS=-I$(IDIR)

ODIR=obj
LDIR=../lib

LIBS=-lncurses

_DEPS=os345config.h os345fat.h os345.h os345lc3.h os345park.h os345signals.h
DEPS=$(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ=os345.o os345interrupts.o os345mmu.o os345p1.o os345p2.o os345p3.o os345p4.o os345p5.o os345p6.o os345fat.o os345lc3.o os345park.o os345signals.o os345semaphores.o os345tasks.o
OBJ=$(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

os345: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

debug: %.c
	gcc -o $@ -g $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~
