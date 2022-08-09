

all:
	gcc ./recordmcount.test.c -DHAS_MAIN -pg -o recordmcount.test.out
	gcc ./recordmcount.test.c -c -pg -o recordmcount.test.o
	#gcc ./recordmcount.c -c -pg -o recordmcount.test.o
	cp recordmcount.test.o recordmcount.o
	gcc ./recordmcount.c -o recordmcount.out -DDEBUG
	./recordmcount.out -w recordmcount.o

clean:
	rm -f *.out *.o


