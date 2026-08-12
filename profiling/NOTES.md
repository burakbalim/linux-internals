# Measurement Notes — perf and gdb

Profiling the `evserver` backends to answer a question the throughput numbers left open:
epoll and io_uring both sat at ~103k req/s while thread-per-connection reached 322k. Why?

Measured on a privileged container, aarch64, 10 cores. Software events only — hardware
counters are not exposed inside the VM.

---

## 1. The event loops were core-bound, and perf proves it

```
BACKEND    throughput   task-clock   CPUs utilized   context-switches
threads    322,146      10348 ms     2.586           1,247,911
epoll      103,555       3783 ms     0.946                   224
uring      103,182       3793 ms     0.948                    96
```

`CPUs utilized` settles the question. epoll and io_uring sit at **0.95 of one core** —
they are single-threaded and saturating the one core they have. The thread backend uses
**2.59 cores**. The comparison was never between I/O models; it was between 1 core and 2.6.

Normalising by CPU actually used:

```
threads    124,573 req/s per CPU
epoll      109,466 req/s per CPU
uring      108,842 req/s per CPU
```

The thread backend is **14% more efficient per core** — the opposite of the folklore.
Blocking `read()` on a dedicated thread is a well-optimised path; it avoids the
`epoll_wait` round trip and the readiness bookkeeping entirely.

So epoll's case rests on the idle-connection memory result, not on CPU efficiency. At this
scale it is not faster, not cheaper per core, and not doing less work per request. It is
cheaper *per idle connection*, which is a different claim and the one that matters for real
servers.

## 2. Context switches: 0.97 per request versus effectively zero

```
threads    1,247,911 switches  =  0.97 per request
epoll            224 switches  =  0.0005 per request
uring             96 switches  =  0.0002 per request
```

Almost exactly **one context switch per request** in the thread model, which is the model
working as designed: the thread blocks in `read()`, the scheduler runs something else, data
arrives, the thread is woken. That is a context switch per round trip, by construction.

The event loops never block on a socket, so they never switch — 224 switches across 414,000
requests. io_uring is lower still (96), because even the readiness notification is
collected from the completion ring rather than through a wakeup.

That 5,500× difference costs the thread backend nothing measurable here. On a machine with
more contention, or with more threads than cores, it would.

## 3. Where the time actually goes — and why io_uring bought nothing

`perf record -F 499 -e cpu-clock -g` on the epoll backend:

```
55.61%  evserver  [kernel.kallsyms]  [k] preempt_count_sub
        --54.67%--_raw_spin_unlock_irqrestore
                  --53.73%--__wake_up_common_lock
                            __wake_up_sync_key
                            sock_def_readable
                            tcp_data_ready
                            tcp_rcv_established
                            tcp_v4_do_rcv
                            tcp_v4_rcv
                            ip_protocol_deliver_rcu
                            ip_local_deliver_finish
```

**Over half the time is in the kernel's TCP receive path**, specifically waking up the
waiter when data arrives. Not in `epoll_wait`. Not in the syscall entry. Not in any code in
this repository.

This explains the io_uring result directly. io_uring cut kernel entries by **8.8×**
(2.20 → 0.25 syscalls per request) and throughput did not move at all — because syscall
entry was never the bottleneck. The cost is in the networking stack processing each packet
and waking the waiter, and that work happens regardless of how the operation was submitted.

The general lesson is the one profiling exists to teach: **optimising the thing you
measured is not the same as optimising the thing that costs.** The syscall counter was a
real measurement of a real difference that happened not to matter here. It would matter
with larger payloads, storage I/O, or high enough connection counts that syscall entry
dominates.

## 4. gdb makes each I/O model visible in one command

`info threads` on a running server, under identical load:

**epoll** — one thread, caught mid-write:

```
* 1  Thread ... "evserver"  in write () from libc.so.6
#1  EpollServer::write_all (len=64, fd=28)      at src/server_epoll.cpp:230
#2  EpollServer::handle_connection (fd=28)      at src/server_epoll.cpp:191
#3  EpollServer::run ()                         at src/server_epoll.cpp:108
```

**threads** — 25 threads, and the model is right there:

```
* 1  "evserver"  in poll () from libc.so.6      <- accept loop
  2  "evserver"  in read () from libc.so.6      <- blocked on its connection
  3  "evserver"  in read () from libc.so.6
  4  "evserver"  in read () from libc.so.6
  ... (24 threads, all in read)
```

**io_uring** — one thread, inside the ring:

```
* 1  "evserver"  in ?? () from liburing.so.2
#1  UringServer::run ()                         at src/server_uring.cpp:116
```

Every thread blocked in `read()` *is* the thread-per-connection model. Reading the source
tells you it should look like that; `info threads` on a live process shows that it does,
which is a different and more convincing kind of knowledge.

This is also the fastest way to diagnose a hung production process: attach, `info threads`,
and see what everything is waiting on. It needs no instrumentation and no restart.

---

## Notes on running perf in a container

- `--privileged` (or `--cap-add=SYS_ADMIN` plus `--security-opt seccomp=unconfined`) is
  required; `perf_event_paranoid` is 2 by default.
- Hardware events (`cycles`, `instructions`, `cache-misses`) are **not available** inside
  Docker Desktop's VM. Use software events: `cpu-clock`, `task-clock`, `context-switches`,
  `page-faults`.
- `-g` needs frame pointers to unwind reliably. These binaries are built `-O2 -g`, which is
  why some frames read `<optimized out>`. Adding `-fno-omit-frame-pointer` gives cleaner
  stacks at a small cost.

## Open questions

- Does a multi-threaded epoll server with `SO_REUSEPORT` beat the thread backend's
  124k req/s per core, now that the per-core numbers are known to be close?
- The TCP wakeup path dominates — does `SO_BUSY_POLL` or a larger payload change the shape?
- Flame graphs proper (`perf script` piped through FlameGraph) instead of `perf report`.
- `perf sched` to see the scheduler's view of those 1.25M context switches.

## Reproducing

From the `evserver` directory, with it built:

```bash
docker run --rm --privileged -v "$PWD":/work -v "$PWD/../profiling":/prof -w /work \
  evserver-dev bash /prof/scripts/profile-evserver.sh epoll

docker run --rm --privileged -v "$PWD":/work -v "$PWD/../profiling":/prof -w /work \
  evserver-dev bash /prof/scripts/inspect-with-gdb.sh threads
```
