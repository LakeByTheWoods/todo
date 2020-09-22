pub usingnamespace @cImport({
    @cDefine("_XOPEN_SOURCE_EXTENDED", "1");
    @cDefine("_GNU_SOURCE", "");
    @cInclude("stdio.h");
    @cInclude("dirent.h");
    @cInclude("fcntl.h");
    @cInclude("locale.h");
    @cInclude("pwd.h");
    @cInclude("stdlib.h");
    @cInclude("string.h");
    @cInclude("sys/types.h");
    @cInclude("time.h");
    @cInclude("unistd.h");
    @cInclude("wchar.h");

    @cDefine("NCURSES_WIDECHAR", "1");
    @cInclude("ncurses.h");
});

pub fn curses_getyx(win: @TypeOf(stdscr), y: *c_int, x: *c_int) void {
    y.* = getcury(win);
    x.* = getcurx(win);
}

pub fn curses_getmaxyx(win: @TypeOf(stdscr), y: *c_int, x: *c_int) void {
    y.* = getmaxy(win);
    x.* = getmaxx(win);
}
