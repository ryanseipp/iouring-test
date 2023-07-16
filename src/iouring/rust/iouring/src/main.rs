use std::collections::VecDeque;
use std::io;
use std::net::TcpListener;
use std::os::fd::{AsRawFd, RawFd};

// https://github.com/tokio-rs/io-uring/blob/66844fbe9b10db40faa31e286ed8fc15fc8ab7f2/src/sys/sys.rs#L121C11-L121C28
const IORING_CQE_F_MORE: u32 = 2;

use io_uring::squeue::Entry;
use io_uring::types::Fd;
use io_uring::{opcode, IoUring, SubmissionQueue};
use slab::Slab;

const RESPONSE: &[u8] =
    b"HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-length: 17\r\n\r\nHave a nice day!\n";

#[derive(Debug)]
enum Op {
    Accept,
    Recv(RawFd, u16),
    Send(RawFd),
    Close,
}

impl From<u64> for Op {
    fn from(value: u64) -> Self {
        unsafe { std::mem::transmute(value) }
    }
}

impl From<Op> for u64 {
    fn from(val: Op) -> Self {
        unsafe { std::mem::transmute(val) }
    }
}

fn push(submission: &mut SubmissionQueue<'_>, entry: Entry, backlog: &mut VecDeque<Entry>) {
    unsafe {
        if submission.push(&entry).is_err() {
            backlog.push_back(entry);
        }
    }
}

fn accept(submission: &mut SubmissionQueue<'_>, fd: RawFd, backlog: &mut VecDeque<Entry>) {
    let entry = opcode::AcceptMulti::new(Fd(fd))
        .build()
        .user_data(Op::Accept.into());
    push(submission, entry, backlog)
}

fn recv(
    submission: &mut SubmissionQueue<'_>,
    fd: RawFd,
    buf: *mut u8,
    buf_len: u32,
    buf_idx: u16,
    backlog: &mut VecDeque<Entry>,
) {
    let entry = opcode::Recv::new(Fd(fd), buf, buf_len)
        .build()
        .user_data(Op::Recv(fd, buf_idx).into());
    push(submission, entry, backlog)
}

fn receive(
    sq: &mut SubmissionQueue<'_>,
    buf_alloc: &mut Slab<[u8; 1024]>,
    fd: RawFd,
    backlog: &mut VecDeque<Entry>,
) {
    let buf_entry = buf_alloc.vacant_entry();
    let buf_index = buf_entry.key() as u16;
    let buf = buf_entry.insert([0u8; 1024]);

    recv(sq, fd, buf.as_mut_ptr(), buf.len() as _, buf_index, backlog)
}

fn send(submission: &mut SubmissionQueue<'_>, fd: RawFd, backlog: &mut VecDeque<Entry>) {
    let entry = opcode::Send::new(Fd(fd), RESPONSE.as_ptr(), RESPONSE.len() as _)
        .build()
        .user_data(Op::Send(fd).into());
    push(submission, entry, backlog)
}

fn close(submission: &mut SubmissionQueue<'_>, fd: RawFd, backlog: &mut VecDeque<Entry>) {
    let entry = opcode::Close::new(Fd(fd))
        .build()
        .user_data(Op::Close.into());
    push(submission, entry, backlog)
}

fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8000")?;
    println!("Server listening on {}", listener.local_addr()?);

    let mut ring = IoUring::new(1024)?;
    let (submitter, mut sq, mut cq) = ring.split();

    let mut backlog = VecDeque::new();
    // TODO: remove
    let mut buf_alloc = Slab::with_capacity(2048);

    // initialize_buffers(&mut sq, &mut bufs, &mut backlog);
    accept(&mut sq, listener.as_raw_fd(), &mut backlog);
    sq.sync();

    loop {
        match submitter.submit_and_wait(1) {
            Ok(_) => (),
            Err(err) => return Err(err),
        }

        loop {
            if cq.is_full() {
                break;
            }

            if sq.is_full() {
                submitter.squeue_wait()?;
            }

            sq.sync();
            match backlog.pop_front() {
                Some(sqe) => {
                    let _ = unsafe { sq.push(&sqe) };
                }
                None => break,
            };
        }

        cq.sync();

        for cqe in &mut cq {
            let event = cqe.user_data();
            let result = cqe.result();

            if result < 0 {
                eprintln!(
                    "CQE {:?} failed: {:?}",
                    Op::from(event),
                    io::Error::from_raw_os_error(-result)
                );
                continue;
            }

            match Op::from(event) {
                Op::Accept => {
                    let conn_fd = result;
                    if cqe.flags() & IORING_CQE_F_MORE == 0 {
                        accept(&mut sq, listener.as_raw_fd(), &mut backlog);
                    }
                    receive(&mut sq, &mut buf_alloc, conn_fd, &mut backlog);
                    // receive_shared(&mut sq, conn_fd, &mut backlog);
                }
                Op::Recv(fd, buf_idx) => {
                    buf_alloc.remove(buf_idx as usize);

                    if result == 0 {
                        close(&mut sq, fd, &mut backlog);
                    }

                    send(&mut sq, fd, &mut backlog);
                }
                Op::Send(fd) => {
                    receive(&mut sq, &mut buf_alloc, fd, &mut backlog);
                }
                Op::Close => {
                    // perform cleanup if needed
                }
            }
        }
    }
}
