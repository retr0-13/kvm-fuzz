const std = @import("std");
const Process = @import("../Process.zig");
const linux = @import("../../linux.zig");
const mem = @import("../../mem/mem.zig");
const scheduler = @import("../../scheduler.zig");
const panic = @import("../../common.zig").panic;
const UserPtr = mem.safe.UserPtr;
const State = Process.State;
const cast = std.zig.c_translation.cast;

fn sys_wait4(
    self: *Process,
    pid: linux.pid_t,
    wstatus: ?UserPtr(*i32),
    options: i32,
    rusage: ?UserPtr(*linux.rusage),
    regs: *Process.UserRegs,
) !linux.pid_t {
    _ = wstatus;
    _ = options;
    _ = rusage;

    self.state = if (pid < -1)
        State{ .waiting_for_any_with_pgid = -pid }
    else if (pid == -1)
        State{ .waiting_for_any = {} }
    else if (pid == 0)
        State{ .waiting_for_any_with_pgid = self.pgid }
    else
        State{ .waiting_for_tgid = pid };

    // TODO: process we are waiting for may have already exited, leading to deadlock
    scheduler.schedule(regs);
    if (scheduler.current() == self)
        panic("deadlock\n", .{});
    return 0;
}

pub fn handle_sys_wait4(
    self: *Process,
    arg0: usize,
    arg1: usize,
    arg2: usize,
    arg3: usize,
    regs: *Process.UserRegs,
) !usize {
    const pid = cast(linux.pid_t, arg0);
    const wstatus = UserPtr(*i32).fromFlatMaybeNull(arg1);
    const options = cast(i32, arg2);
    const rusage = UserPtr(*linux.rusage).fromFlatMaybeNull(arg3);
    const ret = try sys_wait4(self, pid, wstatus, options, rusage, regs);
    return cast(usize, ret);
}
