const std = @import("std");
const assert = std.debug.assert;
const common = @import("../common.zig");
const mem = @import("mem.zig");
const x86 = @import("../x86/x86.zig");
const PageTable = x86.paging.PageTable;
const Allocator = std.mem.Allocator;

pub const Perms = packed struct {
    read: bool = false,
    write: bool = false,
    exec: bool = false,

    pub fn isNone(perms: Perms) bool {
        return !perms.read and !perms.write and !perms.exec;
    }
};

pub const AddressSpace = struct {
    allocator: Allocator,
    page_table: PageTable,
    user_mappings: mem.RegionManager,

    pub fn fromCurrent(allocator: Allocator) AddressSpace {
        const page_table = PageTable.fromCurrent();
        return AddressSpace{
            .allocator = allocator,
            .page_table = page_table,
            .user_mappings = mem.RegionManager.initCheckNotMapped(
                allocator,
                mem.layout.user_mappings_start,
                mem.layout.user_end,
                page_table,
            ),
        };
    }

    pub fn load(self: *AddressSpace) void {
        self.page_table.load();
    }

    pub const MapFlags = packed struct {
        discardAlreadyMapped: bool = false,
        shared: bool = false,
    };

    fn checkRange(addr: usize, length: usize) !void {
        assert(mem.isPageAligned(addr));
        assert(mem.isPageAligned(length));
        assert(length != 0);
        assert(!std.meta.isError(std.math.add(usize, addr, length)));
        if (!mem.safe.isRangeInUserRange(addr, length))
            return error.NotUserRange;
    }

    const MappingError = PageTable.MappingError || error{NotUserRange};

    // Can't return error.AlreadyMapped if flags.discardAlreadyMapped is set.
    pub fn mapRange(
        self: *AddressSpace,
        addr: usize,
        length: usize,
        perms: Perms,
        flags: MapFlags,
    ) MappingError!void {
        try checkRange(addr, length);

        // Mark range as mapped
        try self.user_mappings.setMapped(addr, addr + length);

        // Attempt to allocate physical memory for the range
        const num_frames = @divExact(length, std.mem.page_size);
        var frames = try mem.pmm.allocFrames(self.allocator, num_frames);
        defer self.allocator.free(frames);

        // Get the mapping options acording to memory permissions and mapping flags
        const mapping_options = x86.paging.PageTable.MappingOptions{
            .writable = perms.write,
            .user = true,
            .protNone = perms.isNone(),
            .shared = flags.shared,
            .noExecute = !perms.exec,
            .discardAlreadyMapped = flags.discardAlreadyMapped,
        };

        // Map every page
        var i: usize = 0;
        var page_base: usize = addr;
        errdefer {
            // We won't unmap the mapped pages, as that's what sys_mmap requires,
            // so just free the unused frames
            mem.pmm.freeFrames(frames[i..]);
        }
        while (i < num_frames) : ({
            i += 1;
            page_base += std.mem.page_size;
        }) {
            self.page_table.mapPage(page_base, frames[i], mapping_options) catch |err| switch (err) {
                error.AlreadyMapped => return err,
                else => {
                    // We need to set the rest of the region as not mapped before
                    // returning the error. I don't know if that can fail.
                    self.user_mappings.setNotMapped(page_base, addr + length) catch unreachable;
                    return err;
                },
            };
        }
    }

    pub fn mapRangeAnywhere(
        self: *AddressSpace,
        length: usize,
        perms: Perms,
        flags: MapFlags,
    ) error{OutOfMemory}!usize {
        assert(mem.isPageAligned(length));
        const range_base_addr = self.user_mappings.findNotMapped(length) orelse return error.OutOfMemory;

        self.mapRange(range_base_addr, length, perms, flags) catch |err| switch (err) {
            // These two represent a bug in RegionManager.findNotMapped, as it
            // should always return a not mapped user range.
            error.AlreadyMapped, error.NotUserRange => unreachable,
            error.OutOfMemory => return error.OutOfMemory,
        };
        return range_base_addr;
    }

    const UnmappingError = PageTable.UnmappingError || error{ NotUserRange, OutOfMemory };

    /// Unmap every mapped page in a range of memory. If there's any not mapped
    /// page, it will be ignored, and at the end it will return error.NotMapped.
    pub fn unmapRange(
        self: *AddressSpace,
        addr: usize,
        length: usize,
    ) UnmappingError!void {
        try checkRange(addr, length);

        // Mark range as not mapped
        try self.user_mappings.setNotMapped(addr, addr + length);

        // Unmap every page
        var not_mapped: bool = false;
        const addr_end = addr + length;
        var page_base: usize = addr;
        while (page_base < addr_end) : (page_base += std.mem.page_size) {
            self.page_table.unmapPage(page_base) catch |err| switch (err) {
                error.NotMapped => not_mapped = true,
            };
        }

        if (not_mapped)
            return error.NotMapped;
    }

    const SetPermsError = PageTable.SetPermsError || error{NotUserRange};

    pub fn setRangePerms(
        self: *AddressSpace,
        addr: usize,
        length: usize,
        perms: Perms,
    ) SetPermsError!void {
        try checkRange(addr, length);

        // Set permissions for each page
        const addr_end = addr + length;
        var page_base: usize = addr;
        while (page_base < addr_end) : (page_base += std.mem.page_size) {
            try self.page_table.setPagePerms(page_base, perms);
        }
    }

    pub fn clone(self: AddressSpace) !AddressSpace {
        return AddressSpace{
            .allocator = self.allocator,
            .page_table = try PageTable.clone(self.page_table),
            // TODO: fixme
            .user_mappings = self.user_mappings, //mem.RegionManager.init(self.allocator),
        };
    }
};
