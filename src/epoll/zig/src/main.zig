const std = @import("std");
const builtin = @import("builtin");
const net = std.net;
const os = std.os;

const response: []const u8 = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-length: 17\r\n\r\nHave a nice day!\n";

const port = 8000;
const kernel_backlog = 512;
const max_connections = 2048;
const max_events = 1024;

const EpollEvent = enum { Read, Write, None };

const EpollCtl = enum { Register, Reregister, Deregister };

fn submit_epoll(epoll: i32, fd: i32, comptime ctl: EpollCtl, comptime ev: EpollEvent) !void {
    const common_flags = os.linux.EPOLL.ET | os.linux.EPOLL.RDHUP | os.linux.EPOLL.HUP;
    const control = switch (ctl) {
        EpollCtl.Register => os.linux.EPOLL.CTL_ADD,
        EpollCtl.Reregister => os.linux.EPOLL.CTL_MOD,
        EpollCtl.Deregister => os.linux.EPOLL.CTL_DEL,
    };
    const events = switch (ev) {
        EpollEvent.Read => os.linux.EPOLL.IN | common_flags,
        EpollEvent.Write => os.linux.EPOLL.OUT | common_flags,
        EpollEvent.None => os.linux.EPOLL.ET | common_flags,
    };

    var event: ?os.linux.epoll_event = undefined;
    if (ctl != .Deregister) {
        event = os.linux.epoll_event{ .events = events, .data = .{ .fd = fd } };
    }

    try os.epoll_ctl(epoll, control, fd, &event.?);
}

pub fn handle_read(epoll: i32, server: i32, event: os.linux.epoll_event) !bool {
    var connection_closed = false;

    const buf_size = 1024;
    var buf: [buf_size]u8 = undefined;
    var num_read: usize = 0;

    while (true) {
        if (os.read(event.data.fd, &buf)) |read| {
            if (read == 0) {
                connection_closed = true;
                break;
            }

            num_read += read;
        } else |err| switch (err) {
            error.WouldBlock => break,
            error.OperationAborted => return handle_read(epoll, server, event),
            else => |left_err| return left_err,
        }
    }

    if (num_read != 0) {
        try submit_epoll(epoll, event.data.fd, EpollCtl.Reregister, EpollEvent.Write);
    }

    return connection_closed;
}

pub fn handle_write(epoll: i32, server: i32, event: os.linux.epoll_event) !void {
    if (os.write(event.data.fd, response)) |written| {
        if (written < response.len) {
            return error.WriteZero;
        }

        try submit_epoll(epoll, event.data.fd, EpollCtl.Reregister, EpollEvent.Read);
    } else |err| switch (err) {
        error.WouldBlock => {},
        error.OperationAborted => return handle_write(epoll, server, event),
        else => |left_err| return left_err,
    }
}

pub fn main() !void {
    if (builtin.os.tag != .linux) return error.LinuxRequired;

    const address = try net.Address.parseIp4("127.0.0.1", port);
    const server = try os.socket(address.any.family, os.SOCK.STREAM | os.SOCK.CLOEXEC, os.IPPROTO.TCP);
    defer os.close(server);

    try os.setsockopt(server, os.SOL.SOCKET, os.SO.REUSEADDR, &std.mem.toBytes(@as(c_int, 1)));
    try os.bind(server, &address.any, address.getOsSockLen());
    try os.listen(server, kernel_backlog);

    std.debug.print("Server listening on {}", .{address});

    const epoll = try os.epoll_create1(os.linux.EPOLL.CLOEXEC);
    defer os.close(epoll);

    try submit_epoll(epoll, server, EpollCtl.Register, EpollEvent.Read);

    var events: [max_events]os.linux.epoll_event = undefined;

    while (true) {
        const num_events = os.linux.epoll_wait(epoll, &events, max_events, -1);
        if (num_events < 0) {
            std.debug.print("{}", .{os.linux.getErrno(os.errno)});
            std.process.exit(1);
        }

        for (events[0..num_events]) |event| {
            if (event.data.fd == server) {
                const conn = try os.accept(server, null, null, os.linux.SOCK.NONBLOCK | os.linux.SOCK.CLOEXEC);
                try submit_epoll(epoll, conn, EpollCtl.Register, EpollEvent.Read);
            } else {
                if (event.events & (os.linux.EPOLL.RDHUP | os.linux.EPOLL.HUP) != 0) {
                    try submit_epoll(epoll, event.data.fd, EpollCtl.Deregister, EpollEvent.None);
                    continue;
                }
                if (event.events & os.linux.EPOLL.IN != 0) {
                    if (try handle_read(epoll, server, event)) {
                        try submit_epoll(epoll, event.data.fd, EpollCtl.Deregister, EpollEvent.None);
                    }
                }
                if (event.events & os.linux.EPOLL.OUT != 0) {
                    try handle_write(epoll, server, event);
                }
            }
        }
    }
}
