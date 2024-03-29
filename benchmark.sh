#!/bin/sh

set -e

START_DIR=$PWD

function run_bench() {
    "$@" >2& BENCH=$!
    trap "kill $BENCH" INT
    k6 run k6/simple.js
    kill $BENCH
    sleep 1
}

echo '# Building servers...'
cd src/epoll/rust/epoll
cargo build -qr
cd ../../zig
zig build -Doptimize=ReleaseFast
cd ../../iouring/rust/iouring
cargo build -qr
cd ../../zig
zig build -Doptimize=ReleaseFast
cd ../c
make clean && make

cd $START_DIR

echo '# Running benchmark: Rust epoll'
run_bench ./src/epoll/rust/epoll/target/release/epoll

echo
echo '# Running benchmark: Zig epoll'
run_bench ./src/epoll/zig/zig-out/bin/epoll

echo
echo '# Running benchmark: Rust io_uring'
run_bench ./src/iouring/rust/iouring/target/release/iouring

echo
echo '# Running benchmark: Zig io_uring'
run_bench ./src/iouring/zig/zig-out/bin/io_uring

echo
echo '# Running benchmark: C io_uring'
run_bench ./src/iouring/c/io_uring
