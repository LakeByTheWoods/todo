usingnamespace @import("c.zig");

pub fn u8ToWideString(string: [:0]const u8) [*c]c_int {
    const result = @ptrCast([*c]c_int, @alignCast(@alignOf(*c_int), calloc(strlen(&string[0]), @sizeOf(c_int)).?));
    for (string) |c, i| {
        result[i] = c;
    }
    return result;
}
