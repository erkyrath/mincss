
OBJS = mincss.o
CFLAGS = -Wall

test: $(OBJS) test.o
	cc -o test $(OBJS) test.o

$(OBJS): mincss.h
test.o: mincss.h

clean:
	rm -f *~ *.o test
