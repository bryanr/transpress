CC=gcc
CFLAGS=-Wall -O2 -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE

LIBS=-lbz2 -lz
PROG=transpress
SRCS=main.c
OBJS=$(SRCS:.c=.o)

all: $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LIBS)

clean:
	rm -rf $(OBJS) $(PROG)

