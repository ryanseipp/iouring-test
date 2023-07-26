# Linux Async I/O API Benchmarks

Tests the relative performance of AIO APIs for Linux, specifically epoll and io_uring.

## TODO
| Language | epoll | io_uring |
| -------- | ----- | -------- |
| C        | [ ]   | [ ]      |
| Rust     | [x]   | [x]      |
| Zig      | [x]   | [x]      |

## Benchmark Results

System Specs:
- Kernel: Linux 6.4.2-arch1-1
- Distro: Arch Linux
- CPU: AMD Ryzen 9 7950X
- Memory: 2x 16GB DDR5 @ 6000MT/s

See k6/simple.js for k6 configuration

| Api      | Language | Median Req. Duration | p(99) Req. Duration | p(99.9) Req. Duration | Req/s   |
| -------- | -------- | -------------------- | ------------------- | --------------------- | ------- |
| epoll    | Rust     | 2.03ms               | 3.2ms               | 6.93ms                | 215,752 |
| epoll    | Zig      | 2.03ms               | 3.13ms              | 6.9ms                 | 221,851 |
| io_uring | Rust     | 1.68ms               | 2.28ms              | 6.08ms                | 272,415 |
| io_uring | Zig      | 1.65ms               | 2.17ms              | 5.9ms                 | 279,114 |

## Known issues
Benchmarks are run locally via k6. This isolates issues with networking components, but makes the system much more chatty. The kernel will need to spend more time shuttling packets across the loopback device. Additionally, the NIC is never touched. Until I have the hardware to separate the load generation to a new machine, this will continue to be the case.

Additionally, k6 is inefficient for this test. It's primarily an HTTP benchmarking tool, but we are effectively running TCP servers that send a fixed HTTP response. k6 will also use every core available, which increases the likelihood of impacting the TCP server thermally, with context switches, or via cache misses.

The data written as a response never changes, and thus does not represent a real world use-case. Instead, these benchmarks primarily aim to discover the relative overhead of the different APIs.
