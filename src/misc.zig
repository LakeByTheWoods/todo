const Utf8View = @import("std").unicode.Utf8View;
usingnamespace @import("c.zig");

pub fn u8ToWideString(string: [:0]const u8) [*c]c_int {
    const result = @ptrCast([*c]c_int, @alignCast(@alignOf(*c_int), calloc(string.len, @sizeOf(c_int)).?));
    var ustr = Utf8View.init(string) catch unreachable;
    var it = ustr.iterator();
    var i: usize = 0;
    while (it.nextCodepoint()) |cp| {
        result[i] = cp;
        i += 1;
    }
    return result;
}
