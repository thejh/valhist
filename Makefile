CFLAGS:=-g -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I../cgoodies/include -L../cgoodies -lcgoodies -lm -lX11

.PHONY : all clean

all : valhist

clean :
	rm -f *.o valhist
