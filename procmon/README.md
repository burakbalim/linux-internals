# procmon

A C++17 CLI that reads the `/proc` virtual filesystem to show how processes, threads,
virtual memory and the page cache actually behave on Linux.

It exists to answer questions by measurement rather than by intuition: why VSZ and RSS
diverge, when `mmap` really allocates memory, what the page cache holds, and why the
scheduler takes a task off the CPU.

## Why Docker?

Development happens on macOS, which has no `/proc`. A Linux container has a real procfs,
so the tool runs against real data.

```bash
make docker-shell     # builds the image, mounts the source at /work, opens a shell
make all              # build inside that shell
./build/procmon list
```

One-shot build and run: `make docker-run`.

By default a container only sees its own PID namespace (2 processes). To see every process
on the host VM, use `make docker-shell-hostpid`, which adds `--pid=host`. Comparing the two
is itself a useful demonstration — see [NOTES.md](NOTES.md), section 9.

On Linux you don't need Docker at all; `make all` is enough.

## Commands

| Command | Shows | Reads |
|---|---|---|
| `procmon list [--sort=rss\|cpu\|pid\|threads] [--limit=N]` | Process table | `/proc/<pid>/stat` |
| `procmon show <pid>` | Full breakdown of one process | `stat`, `status`, `cmdline`, `maps` |
| `procmon threads <pid>` | Threads and their context-switch counters | `/proc/<pid>/task/<tid>/` |
| `procmon maps <pid>` | Virtual memory regions, summarised by backing file | `/proc/<pid>/maps` |
| `procmon mem` | System memory, page cache, load average | `/proc/meminfo`, `/proc/loadavg` |

`self` may be used in place of `<pid>`.

Example:

```
$ procmon list --limit=5
 PID  PPID  S  THR     RSS     VSZ     CPU  MAJFLT  COMMAND
-------------------------------------------------------------------
2345  2292  S   27  216.5M    1.5G   1.27s     200  mysqld
1922  1786  S   32  139.5M    2.5G   3.54s    1096  mongod
 451   445  S   35  113.9M    3.2G  14.64s    1396  dockerd
```

## Demos

```bash
./build/mmap_demo 128        # mmap grows VSZ but not RSS, until pages are touched
./build/pagecache_demo 128   # measures page cache residency with mincore(2)
```

`mmap_demo` pauses before unmapping, so you can inspect it from a second shell in the same
container and see the mapping in the process's address space:

```bash
docker exec -it procmon-dev bash          # second shell, same container
./build/procmon maps <pid> | grep mmap_demo
```

Both demos print before/after numbers; the interpretation is written up in
[NOTES.md](NOTES.md).

## Layout

```
include/proc/
  util.hpp             FileDescriptor (RAII), procfs reading, parsing helpers
  process.hpp          ProcessInfo, MemoryRegion, ThreadInfo
  system.hpp           MemInfo, LoadAvg
  mapped_file.hpp      MappedFile (RAII mmap + mincore)
  format.hpp           human-readable formatting, Table
src/                   implementations + main.cpp
demo/                  mmap and page cache demos
```

The RAII wrappers around the syscalls (`FileDescriptor`, `MappedFile`) are written with
move semantics throughout: ownership always sits in exactly one object, and cleanup happens
in the destructor.

Built with `-Wall -Wextra -Wpedantic`, warning-free.

## Notes

Measurements and what they mean: [NOTES.md](NOTES.md)
