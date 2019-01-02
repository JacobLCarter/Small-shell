C = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE -g

SRCES = smallsh.c

OBJS = smallsh.o

smallsh: ${OBJS}
	${C} ${CFLAGS} ${OBJS} -o smallsh -ggdb

smallsh.o: smallsh.c
	${C} ${CFLAGS} -c smallsh.c

clean:
	rm *.o smallsh
