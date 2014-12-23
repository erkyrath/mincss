
OBJS = mincss.o csslex.o cssread.o
CFLAGS = -Wall

test: $(OBJS) test.o
	cc -o test $(OBJS) test.o

$(OBJS): mincss.h cssint.h
test.o: mincss.h cssint.h

clean:
	rm -f *~ *.o test
