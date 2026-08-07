# linux-internals

Small, self-contained tools written to understand how Linux actually behaves —
by measuring it rather than reading about it.

Each subdirectory is an independent project with its own build, README and notes.

| Project | What it explores |
|---|---|
| [`procmon/`](procmon) | Processes, threads, virtual memory, page cache, mmap and scheduling, read straight from `/proc` |

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
```
