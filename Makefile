# simple Makefile to build and test the MasterMind implementation

prg=master-mind
lib=lcdBinary
matches=mm-matches
tester=testm

CC=gcc
AS=as
OPTS=-W -O2

.PHONY: all clean run test unit debug install

all: $(prg) cw2 $(tester)

# debug build with symbols and DEBUG flag
debug: OPTS=-W -g -DDEBUG
debug: all

# create symbolic link for cw2
cw2: $(prg)
	@if [ ! -L cw2 ] ; then ln -s $(prg) cw2 ; fi

# link the main program
$(prg): $(prg).o $(lib).o $(matches).o
	$(CC) -o $@ $^

# compile main program with header dependency
$(prg).o: $(prg).c lcdBinary.h
	$(CC) $(OPTS) -c -o $@ $<

# compile library with header dependency
$(lib).o: $(lib).c lcdBinary.h
	$(CC) $(OPTS) -c -o $@ $<

# generic C compilation
%.o:	%.c
	$(CC) $(OPTS) -c -o $@ $<

# generic assembly compilation
%.o:	%.s
	$(AS) -o $@ $<

# compile matches assembly
$(matches).o: $(matches).s
	$(AS) -o $@ $<

# compile test program
$(tester).o: $(tester).c
	$(CC) $(OPTS) -c -o $@ $<

# link test program
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

# cleanup build artifacts
clean:
	-rm $(prg) $(tester) cw2 *.o