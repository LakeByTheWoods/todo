
all:
	gcc --std=gnu17 -Wall -Werror -Wpedantic -fno-wrapv -fno-strict-aliasing -g -D_GNU_SOURCE -o todo main.c -lncurses -ltinfo

