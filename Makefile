all:
	zig build

linux:
	gcc                                   --std=gnu17 -Wall -Werror -fno-wrapv -fno-strict-aliasing -fsanitize=address -fsanitize=leak -fsanitize=undefined -fno-omit-frame-pointer -g -D_GNU_SOURCE -I/usr/local/Cellar/ncurses/6.1/include/ -o todo main.c -L/usr/local/Cellar/ncurses/6.1/lib/ -lncursesw

mac:
	/usr/local/Cellar/gcc/8.2.0/bin/gcc-8 --std=gnu17 -Wall -Werror -fno-wrapv -fno-strict-aliasing -fsanitize=address -fsanitize=leak -fsanitize=undefined -fno-omit-frame-pointer -g -D_GNU_SOURCE -I/usr/local/Cellar/ncurses/6.1/include/ -o todo main.c -L/usr/local/Cellar/gcc/8.2.0/lib/ -L/usr/local/Cellar/ncurses/6.1/lib/ -lncursesw


notes:
	echo "This isn't an actual build config! What are you doing?"
	To use the bundled libc++ please add the following LDFLAGS:
	LDFLAGS="-L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib"

	llvm is keg-only, which means it was not symlinked into /usr/local,
	because macOS already provides this software and installing another version in
	parallel can cause all kinds of trouble.

	If you need to have llvm first in your PATH run:
	echo 'export PATH="/usr/local/opt/llvm/bin:$PATH"' >> ~/.bash_profile

	For compilers to find llvm you may need to set:
	export LDFLAGS="-L/usr/local/opt/llvm/lib"
	export CPPFLAGS="-I/usr/local/opt/llvm/include"
