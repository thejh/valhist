CFLAGS:=-g -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I../cgoodies/include -L../cgoodies -lcgoodies -lm -lX11 -lXext

.PHONY : all clean

all : valhist slowpipe

clean :
	rm -f *.o valhist slowpipe
