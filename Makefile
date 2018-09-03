
all:
	gcc --std=gnu17 -Wall -Werror -fno-wrapv -fno-strict-aliasing -g -D_GNU_SOURCE -o todo main.c -lncursesw -ltinfo

