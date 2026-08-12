# linux-internals

Small, self-contained tools written to understand how Linux actually behaves —
by measuring it rather than reading about it.

Each subdirectory is an independent project with its own build, README and notes.

| Project | What it explores |
|---|---|
| [`procmon/`](procmon) | Processes, threads, virtual memory, page cache, mmap and scheduling, read straight from `/proc` |
| [`evserver/`](evserver) | The same TCP echo server as threads, epoll level-triggered and edge-triggered, measured side by side |
| [`minicontainer/`](minicontainer) | Namespaces, cgroup v2 limits and pivot_root — a container runtime cut down to the parts that do the work |
| [`elf-lab/`](elf-lab) | ELF layout, the PLT/GOT, lazy binding and `LD_PRELOAD` interposition, demonstrated on a tiny library |
| [`profiling/`](profiling) | `perf` and `gdb` applied to the servers above — where the time actually goes |

## Approach

Every project follows the same shape:

- a working tool, not a snippet — something you can point at a real process
- demos that *change* system state (mapping memory, evicting cache pages) rather
  than only observing it
- a `NOTES.md` recording numbers that were actually measured on the machine, with
  the surprises left in

Development happens in Docker, since the host is macOS and has no procfs. Each
project ships a `Dockerfile` and `make docker-shell`.

## Layout

```
procmon/          /proc-based process and memory inspector
evserver/         TCP echo server in three I/O models, with a load generator
minicontainer/    namespaces + cgroups + pivot_root, in about 400 lines
elf-lab/          experiments on the ELF format and the dynamic linker
profiling/        perf and gdb scripts for the servers above
```
