const std = @import("std");
usingnamespace @import("c.zig");
const todo = @import("todo.zig");
const misc = @import("misc.zig");

pub var dateline_count: i32 = 0;

fn drawPointer(window: *WINDOW, enable_unicode: bool, selection_index: usize, list_index: usize, draw_x: *i32, draw_y: i32) void {
    if (selection_index == list_index)
        _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{F7C6}  " else " >  "), 4)
    else
        _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString("    "), 4);
    draw_x.* += 4;
}

fn drawNotStarted(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(1));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{F62F}  " else " O  "), 4);
    _ = wattroff(window, COLOR_PAIR(1));
    draw_x.* += 4;
}

fn drawPriority(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(2));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{FAD5}  " else " !  "), 4);
    _ = wattroff(window, COLOR_PAIR(2));
    draw_x.* += 4;
}

fn drawDoing(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(4));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{F90B}  " else " @  "), 4);
    _ = wattroff(window, COLOR_PAIR(4));
    draw_x.* += 4;
}

fn drawDone(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(5));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{F633}  " else " %  "), 4);
    _ = wattroff(window, COLOR_PAIR(5));
    draw_x.* += 4;
}
fn drawInReview(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(3));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{E215}  " else " >  "), 4);
    _ = wattroff(window, COLOR_PAIR(3));
    draw_x.* += 4;
}

fn drawDiscarded(window: *WINDOW, enable_unicode: bool, draw_x: *i32, draw_y: i32) void {
    _ = wattron(window, COLOR_PAIR(6));
    _ = mvwaddnwstr(window, draw_y, draw_x.*, misc.u8ToWideString(if (enable_unicode) " \u{FB81}  " else " X  "), 4);
    _ = wattroff(window, COLOR_PAIR(6));

    draw_x.* += 4;
}
pub fn drawNumberSuffix(window: *WINDOW, enable_unicode: bool, number: i32, draw_x: *i32, draw_y: i32) void {
    var to_draw: *wchar_t = undefined;

    const number_mod_10 = @mod(number, 10);
    switch (number_mod_10) {
        1 => {
            to_draw = misc.u8ToWideString("st");
            if (@mod(number_mod_10, 11) == 0)
                to_draw = misc.u8ToWideString("th");
        },

        2 => {
            to_draw = misc.u8ToWideString("nd");
            if (@mod(number_mod_10, 12) == 0)
                to_draw = misc.u8ToWideString("th");
        },

        3 => {
            to_draw = misc.u8ToWideString("rd");
            if (@mod(number_mod_10, 13) == 0)
                to_draw = misc.u8ToWideString("th");
        },

        4...10, 0 => {
            to_draw = misc.u8ToWideString("th");
        },
        else => unreachable,
    }
    _ = mvwaddnwstr(window, draw_y, draw_x.*, to_draw, 2);
    draw_x.* += 2;
}

fn get_week_number(t: *tm) i32 {
    var local = t.*;

    local.tm_mday -= local.tm_wday;
    _ = mktime(&local);

    const prev_sunday = local.tm_yday;
    const week_count = @divFloor(prev_sunday, 7);
    const first_week_length = @mod(prev_sunday, 7);
    if (first_week_length > 0)
        return week_count + 1;
    return week_count;
}

pub fn drawTodoListViewToWindow(window: *WINDOW, enable_unicode: bool, todoList: todo.TodoList, selection_index: usize, scrolling: usize) void {
    var window_width: c_int = undefined;
    var window_height: c_int = undefined;
    curses_getmaxyx(stdscr, &window_height, &window_width);
    _ = werase(stdscr);
    defer _ = wrefresh(stdscr);
    var draw_x: c_int = 0;
    var draw_y: c_int = 1;
    var list_index: usize = 0;

    var max_x: c_int = undefined;
    var max_y: c_int = undefined;

    curses_getmaxyx(window, &max_y, &max_x);
    var space = std.meta.cast(
        [*c]wchar_t,
        calloc(@intCast(usize, max_x + 1), @sizeOf(wchar_t)).?,
    );
    {
        var i: i32 = 0;
        while (i < max_x) : (i += 1)
            _ = wcscat(space, misc.u8ToWideString(" "));
    }

    var scratch_buffer: [64]wchar_t = undefined;
    var week_tracker_tm = std.mem.zeroes(tm);

    var printed_header = false;
    var prev_state = todo.State.Not_Started;

    for (todoList.items) |entry| {
        if (draw_y >= window_height) break;
        if (list_index >= scrolling and
            (list_index - scrolling) < (window_height - 1))
        {
            var tm_ = std.mem.zeroes(tm);
            switch (entry.state) {
                .Discarded, .Priority, .Doing, .In_Review => _ = localtime_r(&entry.time_started, &tm_),
                .Not_Started => _ = localtime_r(&entry.time_added, &tm_),

                .Done => _ = localtime_r(&entry.time_complete, &tm_),
            }

            if (!printed_header or
                (get_week_number(&tm_) < get_week_number(&week_tracker_tm)) or
                (tm_.tm_year < week_tracker_tm.tm_year) or
                (prev_state != entry.state))
            skip_date: {
                prev_state = entry.state;
                week_tracker_tm = tm_;
                week_tracker_tm.tm_mday -= week_tracker_tm.tm_wday;
                const time_at_start_of_week = mktime(&week_tracker_tm);
                if (!printed_header) {
                    printed_header = true;
                    break :skip_date;
                }
                dateline_count += 1;

                var date_string_length = wcsftime(&scratch_buffer[0], scratch_buffer.len, misc.u8ToWideString(" %Y %B %e"), &week_tracker_tm);
                _ = mvwaddnwstr(window, draw_y, draw_x, &scratch_buffer[0], @intCast(c_int, date_string_length));
                draw_x += @intCast(c_int, date_string_length);

                drawNumberSuffix(window, enable_unicode, week_tracker_tm.tm_mday, &draw_x, draw_y);

                const time_at_end_of_week = time_at_start_of_week + 60 * 60 * 24 * 7;
                var temp_tm: tm = undefined;
                _ = localtime_r(&time_at_end_of_week, &temp_tm);

                date_string_length = wcsftime(&scratch_buffer[0], scratch_buffer.len, misc.u8ToWideString(" .. %B %e"), &temp_tm);
                _ = mvwaddnwstr(window, draw_y, draw_x, &scratch_buffer[0], @intCast(c_int, date_string_length));
                draw_x += @intCast(c_int, date_string_length);

                drawNumberSuffix(window, enable_unicode, temp_tm.tm_mday, &draw_x, draw_y);

                draw_y += 1;
                draw_x = 0;

                if (draw_y >= window_height)
                    break;
            }

            if (selection_index == list_index)
                _ = wattron(window, WA_STANDOUT);

            // Clear whole line
            _ = mvwaddnwstr(window, draw_y, 0, space, max_x);

            drawPointer(window, enable_unicode, selection_index, list_index, &draw_x, draw_y);

            const day_string_length = wcsftime(&scratch_buffer[0], scratch_buffer.len, misc.u8ToWideString(" %a "), &tm_);
            _ = mvwaddnwstr(window, draw_y, draw_x, &scratch_buffer[0], @intCast(c_int, day_string_length));
            draw_x += @intCast(c_int, day_string_length);

            switch (entry.state) {
                .Not_Started => drawNotStarted(window, enable_unicode, &draw_x, draw_y),
                .Priority => drawPriority(window, enable_unicode, &draw_x, draw_y),
                .Doing => drawDoing(window, enable_unicode, &draw_x, draw_y),
                .Done => drawDone(window, enable_unicode, &draw_x, draw_y),
                .In_Review => drawInReview(window, enable_unicode, &draw_x, draw_y),
                .Discarded => drawDiscarded(window, enable_unicode, &draw_x, draw_y),
            }

            _ = mvwaddwstr(window, draw_y, draw_x, entry.text);
            if (selection_index == list_index)
                _ = wattroff(window, WA_STANDOUT);
            draw_y += 1;
            draw_x = 0;
        }
        list_index += 1;
    }

    _ = free(space);
}
