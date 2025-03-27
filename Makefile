# Makefile for MasterMind implementation

prg=master-mind
lib=lcdBinary
matches=mm-matches
tester=testm

CC=gcc
AS=as
OPTS=-W -O2

.PHONY: all clean run test unit debug install

all: $(prg) cw2 $(tester)

debug: OPTS=-W -g -DDEBUG
debug: all

cw2: $(prg)
	@if [ ! -L cw2 ] ; then ln -s $(prg) cw2 ; fi

$(prg): $(prg).o $(lib).o $(matches).o
	$(CC) -o $@ $^

$(prg).o: $(prg).c lcdBinary.h
	$(CC) $(OPTS) -c -o $@ $<

$(lib).o: $(lib).c lcdBinary.h
	$(CC) $(OPTS) -c -o $@ $<

%.o:	%.c
	$(CC) $(OPTS) -c -o $@ $<

%.o:	%.s
	$(AS) -o $@ $<

# Compile mm-matches.s to mm-matches.o
$(matches).o: $(matches).s
	$(AS) -o $@ $<

# Compile testm.c to testm.o
$(tester).o: $(tester).c
	$(CC) $(OPTS) -c -o $@ $<

# Link testm.o with mm-matches.o to create testm
$(tester): $(tester).o $(matches).o
	$(CC) -o $@ $^

# run the program with debug option to show secret sequence
run:
	sudo ./$(prg) -d

# do unit testing on the matching function
unit: cw2
	sh ./test.sh

# testing the C vs the Assembler version of the matching fct
test:	$(tester)
	./$(tester)

# install the program
install: $(prg)
	install -m 755 $(prg) /usr/local/bin/

clean:
	-rm $(prg) $(tester) cw2 *.o