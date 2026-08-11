# Measurement Notes — I/O Models

Every number here was measured on the machine with `loadgen` and `/proc`, on a
10-core aarch64 Linux 6.x container. Payload 64 bytes, 8 active connections, 5 seconds.

The headline result is not the one the exercise was set up to produce, which is the
interesting part.

---

## 1. epoll lost the throughput benchmark, and that is not a flaw in epoll

With 8 active connections and no idle ones:

| Backend | Throughput | p50 latency | p99 latency |
|---|---|---|---|
| thread-per-connection | **371,333 req/s** | 8.0 µs | 120.9 µs |
| epoll (level-triggered) | 98,949 req/s | 73.8 µs | 173.4 µs |
| epoll (edge-triggered) | 91,454 req/s | 79.2 µs | 309.5 µs |

The thread backend is **3.75× faster**. The reason is not subtle once stated: the
machine has **10 cores**, the thread server runs 8 connections on 8 threads and uses
8 of them, while the epoll server is **single-threaded and uses one**.

So this table compares 8 cores against 1 core, and epoll still gets within a factor of
four. The honest conclusion is that **epoll is not a throughput optimisation**. It is a
way to handle many connections in one thread. Comparing it to threads on raw throughput
without matching the parallelism measures the wrong thing.

The fair version of this benchmark needs a multi-threaded epoll server (several event
loops, each with `SO_REUSEPORT`). That is the obvious next iteration.

## 2. Where the models actually diverge: idle connections

Repeating the run with **500 additional idle connections** — opened, then left silent,
which is what real servers hold:

| Backend | VSZ | RSS | Threads | Throughput |
|---|---|---|---|---|
| thread-per-connection | **9.15 GB** | **58.7 MB** | **509** | 297,427 req/s |
| epoll (level-triggered) | 5.2 MB | 2.6 MB | 1 | 103,369 req/s |
| epoll (edge-triggered) | 5.2 MB | 2.6 MB | 1 | 102,523 req/s |

For epoll, **the numbers did not move at all** — VSZ 5.2 MB and RSS 2.6 MB, identical to
the run with zero idle connections. An idle connection is one file descriptor and one
entry in an epoll set. It costs nothing until data arrives.

For the thread backend, 500 idle connections cost:
- **58.7 MB of RSS**, ~113 kB per thread of actually-touched memory
- **9.15 GB of virtual address space**
- a 20% throughput drop (371k → 297k) purely from having idle threads in the system —
  the scheduler now has 509 tasks to consider instead of 9

This is the result the exercise was for. Not "epoll is faster", but **epoll's cost per
idle connection is approximately zero, and thread-per-connection's is a whole thread**.

## 3. That 9.15 GB of VSZ breaks down exactly

Worth pulling apart, because "8 MB stack per thread" only explains part of it:

```
508 threads x 8 MB stack                        = 3.97 GB
glibc malloc arenas: 8 x 10 cores x 64 MB       = 4.97 GB
                                          total ≈ 8.94 GB  (measured 9.15 GB)
```

glibc gives threads their own malloc arenas to avoid lock contention, each reserving
64 MB of address space, capped at `8 × core count` arenas. So a threaded server pays
address space for **cores it has, not just threads it spawned**.

None of this is resident — RSS is only 58.7 MB — which is exactly the VSZ/RSS distinction
from `procmon`. But address space is still finite, and this is how a 32-bit process runs
out of it long before it runs out of RAM.

## 4. Edge-triggered costs an extra syscall per request

Counters from the server side, syscalls per request:

| Backend | read() | write() | epoll_wait | per request |
|---|---|---|---|---|
| threads | 1,485,751 | 1,485,743 | 9 | **2.00** |
| epoll LT | 395,899 | 395,891 | 78,261 | **2.20** |
| epoll ET | **731,745** | 365,942 | 72,276 | **3.20** |

Edge-triggered did **2× the reads** for the same number of requests, and `EAGAIN` was
returned 365,795 times — once per request.

That is inherent to the mode, not a bug: ET only reports readiness when it *changes*, so
the loop must keep reading until `EAGAIN` to know the socket is drained. For a request/
response protocol where one read gets the whole message, that second read is pure
overhead. And it showed: ET was slightly *slower* than LT (91k vs 99k), with a worse p99
(309 µs vs 173 µs).

**ET pays off when a single wakeup carries lots of data** — bulk transfers, or many
messages batched into one arrival — where one wakeup amortises across many reads. For
small request/response traffic, level-triggered is both simpler and faster.

The dangerous part of ET is not performance, it is correctness: forget to drain, and the
leftover bytes are never reported again. The connection just hangs. Same for `accept()` —
one `accept` per wakeup silently drops connections that arrived in the same instant.

## 5. epoll_wait wakeups are not one per request

LT: 395,891 requests over 78,261 `epoll_wait` calls — **~5 requests per wakeup**.

Each wakeup returned several ready descriptors at once, which is the other half of why
epoll scales: the syscall cost is amortised across everything that became ready together.
Under heavier load this ratio improves further. It is also why `wait calls` for the thread
backend is 9 — it only ever polls to `accept`, since each connection blocks in its own
thread.

## 6. signalfd makes shutdown ordinary

`SIGINT` is blocked with `sigprocmask` and read from a `signalfd` registered in the same
epoll set as the sockets. Shutdown is then just another readable descriptor handled in the
normal flow of the loop.

The alternative — an async signal handler — may only call async-signal-safe functions, so
it typically sets a `volatile sig_atomic_t` flag that the loop polls, with a race around
`epoll_wait` blocking indefinitely just after the flag is set. `signalfd` removes both the
race and the restriction.

Blocking the signal first is mandatory: without `sigprocmask` the default action kills the
process before `signalfd` ever reports anything.

---

## Open questions

- Multi-threaded epoll with `SO_REUSEPORT`: does it reach the thread backend's throughput
  while keeping the idle-connection cost near zero? This is the missing comparison.
- io_uring: the promise is fewer syscalls per request than 2.00. Worth measuring against
  the table in section 4.
- Does `EPOLLEXCLUSIVE` change the accept pattern under multiple event loops?
- Where does the 24 ms max latency in the thread backend come from — scheduler preemption,
  or the loadgen's own contention?

## Reproducing

```bash
make docker-shell
make all
./build/evserver --backend=epoll --port=9100 &
./build/loadgen --port=9100 --threads=8 --idle=500 --seconds=5
grep -E '^(VmRSS|VmSize|Threads)' /proc/<server pid>/status   # while load is running
```

Sample the server's `/proc` **during** the run: the thread backend's threads exit as soon
as connections close, so measuring afterwards shows `Threads: 1` and hides the whole effect.

`strace` and `perf` need extra capabilities — use `make docker-shell-trace`.
