const std = @import("std");
const builtin = @import("builtin");
const net = std.net;
const os = std.os;
const IO_Uring = os.linux.IO_Uring;
const io_uring_cqe = os.linux.io_uring_cqe;

const Event = packed struct {
    fd: i32,
    op: Op,
};

const Op = enum(u32) {
    Accept,
    Recv,
    Send,
    Close,
};

const response: []const u8 = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-length: 17\r\n\r\nHave a nice day!\n";

const port = 8000;
const kernel_backlog = 1024;
const max_connections = 2048;
const max_buffer = 1024;
var buffers: [max_connections][max_buffer]u8 = undefined;

fn accept(ring: *IO_Uring, fd: os.fd_t, addr: *os.sockaddr, addrlen: *os.socklen_t) !void {
    var user_data = get_user_data(fd, .Accept);
    _ = try ring.accept(user_data, fd, addr, addrlen, 0);
}

fn recv(ring: *IO_Uring, fd: os.fd_t) !void {
    var user_data = get_user_data(fd, .Recv);
    _ = try ring.recv(user_data, fd, .{ .buffer = get_buffer(fd) }, os.MSG.NOSIGNAL);
}

fn send(ring: *IO_Uring, fd: os.fd_t) !void {
    var user_data = get_user_data(fd, .Send);
    _ = try ring.send(user_data, fd, response, os.MSG.NOSIGNAL);
}

// normally we'd free the buffer for the connection
fn close(ring: *IO_Uring, fd: os.fd_t) !void {
    var user_data = get_user_data(fd, .Close);
    _ = try ring.close(user_data, fd);
}

fn get_buffer(fd: os.fd_t) []u8 {
    return buffers[@mod(@intCast(usize, fd), max_connections)][0..];
}

fn get_user_data(fd: os.fd_t, op: Op) u64 {
    var event: Event = .{ .fd = fd, .op = op };
    var user_data = @bitCast(u64, event);
    return user_data;
}

pub fn main() !void {
    if (builtin.os.tag != .linux) return error.LinuxRequired;

    const address = try net.Address.parseIp4("127.0.0.1", port);
    const server = try os.socket(address.any.family, os.SOCK.STREAM | os.SOCK.CLOEXEC, os.IPPROTO.TCP);
    errdefer os.close(server);

    try os.setsockopt(server, os.SOL.SOCKET, os.SO.REUSEADDR, &std.mem.toBytes(@as(c_int, 1)));
    try os.bind(server, &address.any, address.getOsSockLen());
    try os.listen(server, kernel_backlog);

    std.debug.print("Server listening on {}", .{address});

    var ring = try IO_Uring.init(kernel_backlog, 0);
    defer ring.deinit();

    var cqes: [kernel_backlog]io_uring_cqe = undefined;
    var accept_addr: os.sockaddr = undefined;
    var accept_addrlen: os.socklen_t = @sizeOf(@TypeOf(accept_addr));

    for (buffers) |_, index| {
        buffers[index] = [_]u8{0} ** max_buffer;
    }

    try accept(&ring, server, &accept_addr, &accept_addrlen);

    while (true) {
        const count = try ring.copy_cqes(cqes[0..], 0);

        var i: usize = 0;
        while (i < count) : (i += 1) {
            const cqe = cqes[i];
            const event = @bitCast(Event, cqe.user_data);

            if (cqe.res < 0) {
                var abortServer = false;

                switch (cqe.err()) {
                    os.E.PIPE => std.debug.print("EPIPE {}\n", .{cqe}),
                    os.E.CONNRESET => std.debug.print("ECONNRESET {}\n", .{cqe}),
                    else => {
                        std.debug.print("ERROR {}\n", .{cqe});
                        abortServer = true;
                    },
                }

                if (abortServer) {
                    os.exit(1);
                } else {
                    try close(&ring, event.fd);
                }
            }

            switch (event.op) {
                .Accept => {
                    try accept(&ring, server, &accept_addr, &accept_addrlen);
                    try recv(&ring, cqe.res);
                },
                .Recv => {
                    if (cqe.res != 0) {
                        try send(&ring, event.fd);
                    }
                },
                .Send => {
                    try recv(&ring, event.fd);
                },
                .Close => {},
            }
        }
        _ = try ring.submit_and_wait(1);
    }
}
