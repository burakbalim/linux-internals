# profiling

Two scripts that profile the `evserver` backends with `perf` and inspect them with `gdb`,
to answer a question the throughput numbers left open: epoll and io_uring both sat at
~103k req/s while thread-per-connection reached 322k. Why?

The short answer, from `perf stat`: the event loops were saturating **one core** while the
thread backend used **2.6**. The comparison was never between I/O models.

The more useful answer, from `perf record`: **over half the time is in the kernel's TCP
wakeup path**, which is why io_uring cutting syscalls by 8.8× changed nothing.

Full findings: [NOTES.md](NOTES.md)

## Running

Both scripts expect to run from the `evserver` directory with it already built, and need
`--privileged` (perf and gdb both need capabilities a default container does not have):

```bash
cd ../evserver && make all

docker run --rm --privileged -v "$PWD":/work -v "$PWD/../profiling":/prof -w /work \
  evserver-dev bash /prof/scripts/profile-evserver.sh epoll

docker run --rm --privileged -v "$PWD":/work -v "$PWD/../profiling":/prof -w /work \
  evserver-dev bash /prof/scripts/inspect-with-gdb.sh threads
```

| Script | What it does |
|---|---|
| `profile-evserver.sh` | `perf stat` for CPU utilisation and context switches, then `perf record -g` for where the time goes |
| `inspect-with-gdb.sh` | attaches to a live server and shows `info threads` and a backtrace |

`inspect-with-gdb.sh threads` is the one worth running first: 24 threads all sitting in
`read()` is the thread-per-connection model made visible in a single command.

## Caveats

Hardware counters (`cycles`, `cache-misses`) are not available inside Docker Desktop's VM,
so these use software events only. On a real Linux host, drop `-e cpu-clock` and perf will
use hardware sampling.
