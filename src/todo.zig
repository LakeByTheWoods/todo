const std = @import("std");
const assert = @import("std").debug.assert;
const misc = @import("misc.zig");
usingnamespace @import("c.zig");

const warn = std.debug.warn;

pub const List = std.ArrayList(Entry);
pub const Allocator = std.mem.Allocator;

fn log(comptime arg: []const u8) void {
    warn(arg, .{});
}

/// Cleanup: Must call deinit on result
pub fn loadList(allocator: *Allocator, file_path: [*c]const u8) !List {
    var result = List.init(allocator);

    //std.debug.warn("Loading Todo List: '{}'\n", .{file_path[0..strlen(&file_path[0])]});
    // FIXME: Use zig std io
    var fd = open(file_path, O_RDONLY);
    defer _ = close(fd);

    // FIXME: assert is too heavy handed
    assert(fd != -1);

    var offset_end: off_t = lseek(fd, 0, SEEK_END);
    assert(offset_end != -1);
    if (lseek(fd, 0, SEEK_SET) == -1)
        return error.LSeekError;

    var file_contents = calloc(@intCast(c_ulong, offset_end) + 1, 1);
    defer free(file_contents);
    if (read(fd, file_contents, @intCast(usize, offset_end)) != offset_end)
        return error.FileReadFailed;

    var wide_file_contents = calloc(@intCast(c_ulong, offset_end) + 1, @sizeOf(wchar_t)) orelse return error.OutOfMem;
    defer free(wide_file_contents);
    var start_ptr: [*c]wchar_t = std.meta.cast([*c]wchar_t, wide_file_contents);
    _ = swprintf(start_ptr, @intCast(usize, offset_end) + 1, &([_]c_int{ '%', 's', 0 })[0], file_contents);
    var save_ptr: [*c]wchar_t = undefined;
    while (true) {
        var line = wcstok(start_ptr, &([_]c_int{ '\n', 0 })[0], &save_ptr);
        start_ptr = null;
        if (line == null)
            break;

        var time_added: time_t = wcstoll(line, &line, 16);
        var time_started: time_t = wcstoll(line, &line, 16);
        var time_complete: time_t = wcstoll(line, &line, 16);
        var statell: c_longlong = wcstoll(line, &line, 10);

        const state = @intToEnum(State, statell);

        line += wcsspn(line, &([_]c_int{ ' ', 0 })[0]); // reject spaces until actual text

        try result.append(.{
            .time_added = time_added,
            .time_started = time_started,
            .time_complete = time_complete,
            .state = state,
            .text = wcsdup(line),
        });
    }

    return result;
}

fn compare(conext: void, first: Entry, secnd: Entry) bool {
    if (first.state == secnd.state) {
        // identical states, compare times
        switch (first.state) {
            .Discarded, .Priority, .Doing, .In_Review => return first.time_started > secnd.time_started,

            .Not_Started => return first.time_added > secnd.time_added,

            .Done => return first.time_complete > secnd.time_complete,
        }
        return false;
    }

    if (first.state == .In_Review) {
        return true;
    } else if (secnd.state == .In_Review)
        return false;

    if (first.state == .Priority) {
        return true;
    } else if (secnd.state == .Priority)
        return false;

    if (first.state == .Doing) {
        return true;
    } else if (secnd.state == .Doing)
        return false;

    if (first.state == .Not_Started) {
        return true;
    } else if (secnd.state == .Not_Started)
        return false;

    if (first.state == .Done) {
        return true;
    } else if (secnd.state == .Done)
        return false;

    if (first.state == .Discarded) {
        return true;
    } else if (secnd.state == .Discarded)
        return false;
    return true;
}

pub fn sort(items: []Entry) void {
    std.sort.sort(Entry, items, {}, compare);
}

pub const State = enum(c_longlong) {
    Not_Started = 0,
    Priority = 1,
    Doing = 2,
    Done = 3,
    In_Review = 4,
    Discarded = 5,
};

pub const Entry = struct {
    time_added: time_t,
    time_started: time_t,
    time_complete: time_t,
    state: State,
    text: *wchar_t,
};

pub fn save(list: List, file_path: []u8) void {
    const file = fopen(&file_path[0], "w");
    defer _ = fclose(file);

    const format = misc.u8ToWideString("%X %X %X %X %ls\n");
    defer free(format);

    for (list.items) |entry| {
        _ = fwprintf(
            file,
            &format[0],
            entry.time_added,
            entry.time_started,
            entry.time_complete,
            entry.state,
            entry.text,
        );
    }
}
