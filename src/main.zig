const std = @import("std");
const warn = @import("std").debug.warn;
const assert = @import("std").debug.assert;
const draw = @import("draw.zig");
const todo = @import("todo.zig");
const misc = @import("misc.zig");

usingnamespace @import("c.zig");

fn getHomeDir() [*c]u8 {
    var homedir = getenv("HOME");
    if (homedir == null) {
        var pw = getpwuid(getuid());
        homedir = pw.*.pw_dir;
    }

    var result: [*c]u8 = undefined;
    var r = asprintf(&result, "%s/", homedir);
    assert(r >= 0);
    return result;
}

const Config = struct {
    enable_unicode: bool = false,
    enter_graphical_mode: bool = true,
    listfile: [*c]u8 = null,
};

fn cStrToSlice(cstr: [*c]u8) []u8 {
    return cstr[0..strlen(cstr)];
}

pub fn main() anyerror!void {
    var result = setlocale(LC_ALL, "");
    assert(result != 0);
    result = setlocale(LC_CTYPE, "");
    assert(result != 0);

    var homedir = getHomeDir();
    defer free(homedir);

    const default_listfile = "todolist";
    var config = (Config){};
    result = asprintf(&config.listfile, "%s%s", homedir, default_listfile);
    defer free(config.listfile);

    assert(result >= 0);

    {
        const config_filename: [*c]const u8 = &".config/todo.config"[0];
        var config_dir: [*c]u8 = undefined;
        _ = asprintf(&config_dir, "%s%s", homedir, config_filename);
        defer free(config_dir);
        assert(config_dir != null);

        if (fopen(config_dir, "r")) |config_file| {
            defer _ = fclose(config_file);
            var key = [_:0]u8{0} ** 101;
            var value = [_:0]u8{0} ** 101;
            _ = printf("loaded config file %s\n", config_dir);
            while (fscanf(config_file, "%100s %100s\n", &key[0], &value[0]) > 0) {
                _ = printf("CONFIG: %s = %s\n", key, value);

                if (strcmp(&key, "unicode") == 0) {
                    if (strcmp(&value, "true") == 0) {
                        _ = printf("ENABLING UNICODE\n");
                        config.enable_unicode = true;
                    } else if (strcmp(&value, "false") == 0) {
                        _ = printf("DISS UNICODE\n");
                        config.enable_unicode = false;
                    } else {
                        _ = fprintf(stderr, "Unkown value for 'unicode' in config %s\n", value);
                        exit(EXIT_FAILURE);
                    }
                } else if (strcmp(&key, "listfile") == 0) {
                    _ = free(config.listfile);
                    config.listfile = strdup(&value);
                    _ = printf("Listfile is %s\n", value);
                } else {
                    _ = fprintf(stderr, "Unkown key in config '%s' = '%s'\n", key, value);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    // TODO a better default allocator that isn't as wasteful!
    //const args = try std.process.argsAlloc(std.heap.page_allocator);
    //defer std.process.argsFree(std.heap.page_allocator, args);
    //for (args) |arg, i| {
    //    switch (arg[0]) {
    //
    //    }
    //    std.debug.warn("{}: {}\n", .{i, arg});
    //}

    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = &arena.allocator;

    var global_listing = try todo.loadList(allocator, config.listfile);
    defer global_listing.deinit();
    const count = global_listing.items.len;
    std.debug.warn("COUNT = {}\n", .{count});

    const args = try std.process.argsAlloc(std.heap.page_allocator);
    defer std.process.argsFree(std.heap.page_allocator, args);

    {
        var tp: timespec = undefined;
        if (clock_gettime(CLOCK_REALTIME, &tp) == -1)
            return error.ClockGetTime;
        const tnow = tp.tv_sec;

        for (args[1..]) |arg, i| {
            switch (arg[0]) {
                '-' => {}, // config
                else => {
                    var text: *wchar_t = std.meta.cast(*c_int, calloc(strlen(&arg[0]) + 1, @sizeOf(wchar_t)).?);
                    if (mbstowcs(text, &arg[0], strlen(&arg[0]) + 1) == -1)
                        return error.InvalidMultibyteSequence;

                    var new = try global_listing.addOne();

                    new.* = .{
                        .time_added = tnow,
                        .time_started = 0,
                        .time_complete = 0,
                        .state = todo.State.Not_Started,
                        .text = text,
                    };
                },
            }
            std.debug.warn("ARRGH {}: {}\n", .{ i, arg });
        }
        todo.sort(global_listing.items);
    }

    defer {
        todo.sort(global_listing.items);
        todo.save(global_listing, config.listfile[0..strlen(config.listfile)]);
    }

    if (!config.enter_graphical_mode)
        return;

    std.debug.warn("All your codebase are belong to us.\nHome={}\nlistfile={}\n", .{ cStrToSlice(homedir), cStrToSlice(config.listfile) });

    _ = initscr(); // start up ncurses
    defer _ = endwin();
    _ = cbreak(); // don't wait for return when getting chars
    _ = noecho(); // don't echo keypresses
    _ = nonl(); // we wan't to be able to detect the return key
    _ = clear(); // make sure screen is cleared because some implementations don't do it automatically
    _ = curs_set(0); // set cursor invisible
    _ = start_color();
    _ = init_pair(1, COLOR_WHITE, COLOR_BLACK);
    _ = init_pair(2, COLOR_BLACK, COLOR_RED);
    _ = init_pair(3, COLOR_BLUE, COLOR_WHITE);
    _ = init_pair(4, COLOR_CYAN, COLOR_BLACK);
    _ = init_pair(5, COLOR_GREEN, COLOR_BLACK);
    _ = init_pair(6, COLOR_RED, COLOR_BLACK);

    _ = intrflush(stdscr, false);
    _ = keypad(stdscr, true); // get special keys
    _ = idlok(stdscr, true);
    _ = leaveok(stdscr, true);
    _ = scrollok(stdscr, true); // enable window scrolling
    _ = wtimeout(stdscr, -1);

    var window_width: c_int = undefined;
    var window_height: c_int = undefined;
    var selection_index: usize = 0;
    var scrolling: usize = 0;
    var ch: wint_t = undefined;

    // Draw the list at least once
    draw.drawTodoListViewToWindow(stdscr, config.enable_unicode, global_listing, selection_index, scrolling);

    _ = get_wch(&ch);
    while (ch != 'q') : (_ = get_wch(&ch)) {
        curses_getmaxyx(stdscr, &window_height, &window_width);
        if (ch == KEY_UP) {
            if (selection_index > 0)
                selection_index -= 1;
            if (selection_index < scrolling) {
                if (scrolling == 0) {
                    selection_index = 0;
                } else
                    scrolling -= 1;
            }
        }
        if (ch == KEY_DOWN) {
            const list_count = global_listing.items.len;
            selection_index += 1;
            if (selection_index >= list_count)
                selection_index = list_count - 1;
            if (selection_index - scrolling >= window_height - draw.dateline_count - 1)
                scrolling += 1;
        }
        if (ch == KEY_LEFT) {
            var selected = &global_listing.items[selection_index];
            selected.*.state = .Not_Started;
        }
        if (ch == KEY_RIGHT) {
            var item = &global_listing.items[selection_index];
            switch (item.state) {
                .Not_Started, .Priority => {
                    item.state = .Doing;

                    var tp: timespec = undefined;
                    _ = clock_gettime(CLOCK_REALTIME, &tp);
                    item.time_started = tp.tv_sec;
                },

                .Doing => {
                    item.state = .In_Review;
                },

                .In_Review => {
                    item.state = .Done;

                    var tp: timespec = undefined;
                    _ = clock_gettime(CLOCK_REALTIME, &tp);
                    item.time_complete = tp.tv_sec;
                },

                .Done => {},

                .Discarded => {
                    item.state = .Not_Started;
                },
            }
        }
        if (ch == 'd') {
            var item = global_listing.items[selection_index];
            item.state = .Discarded;
        }
        if (ch == '!') {
            var item = global_listing.items[selection_index];
            item.state = .Priority;
            var tp: timespec = undefined;
            _ = clock_gettime(CLOCK_REALTIME, &tp);
            item.time_started = tp.tv_sec;
        }
        draw.drawTodoListViewToWindow(stdscr, config.enable_unicode, global_listing, selection_index, scrolling);
    }
}
