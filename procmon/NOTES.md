# Measurement Notes — Linux Internals

Topics: processes, threads, virtual memory, page cache, mmap, scheduler.

Every number below was actually measured with the `procmon` binary and the demos in this
repository (Docker, aarch64, Linux 6.x) — none of it is quoted from memory.

---

## 1. `/proc` is a window into the kernel, not a filesystem

`/proc` occupies no disk space; every read produces a fresh answer generated in the kernel.
Two practical consequences:

- `stat()` reports **size 0** for these files. You have to `read()` in a loop until EOF —
  trying to read `st_size` bytes gives you nothing. (That's what `read_file` in
  `src/util.cpp` is for.)
- Races are normal when walking the process list: a process can exit while you scan, and
  `/proc/<pid>/stat` then fails to open. The correct behaviour is to skip it, not to error out.

## 2. The trap in parsing `/proc/<pid>/stat`

Field 2 (`comm`) is wrapped in parentheses and **may contain both spaces and `)`**. A naive
`split(" ")` therefore breaks. The correct approach: find the **last** `)` and count fields
from there. A process named `(my proc)` shifts every field in a naive parser.

Commonly used fields (1-based): 4=ppid, 10=minflt, 12=majflt, 14=utime, 15=stime,
18=priority, 19=nice, 20=num_threads, 22=starttime, 23=vsize, 24=rss, 39=processor, 41=policy.

Three more traps:
- **Field 24 (rss) is a page count, not bytes.** Multiply by `sysconf(_SC_PAGESIZE)`.
- Times are in clock ticks, not seconds; divide by `sysconf(_SC_CLK_TCK)` (usually 100).
- Field 18 (priority) is the kernel's raw value: nice 0 shows as **20**, nice -20 shows as 0.

## 3. VSZ is not RSS — what the mmap demo shows

The output of `mmap_demo 64` is the clearest possible evidence:

| Stage | VSZ | RSS | minflt |
|---|---|---|---|
| Start | 5.1M | 2.8M | 165 |
| After 64MB `mmap`, untouched | **69.1M** | **3.0M** | 428 |
| After touching every page | 69.1M | **67.0M** | 1452 |

- `mmap` **allocates no memory**; it reserves a region of the address space. VSZ +64M, RSS flat.
- A physical page is attached only when the address is accessed, via a **page fault**
  (demand paging).
- So "VSZ is 2GB" is not cause for alarm. RSS is the number that means something.

**The fault-around surprise:** 64MB is 16384 pages, yet the minor fault count rose by only
~1024. On each fault the kernel maps not one page but roughly 16 surrounding pages
(fault-around). 16384/16 ≈ 1024. It's an optimisation, because the per-fault cost is high
relative to mapping an extra page.

## 4. Minor vs major faults — this is the distinction that matters

- **Minor fault:** the page is already in RAM (page cache, or a shared page); it just needed
  wiring into the page table. Cheap.
- **Major fault:** the page had to be read from disk. Expensive — milliseconds, not microseconds.

That's why `procmon list` has a `MAJFLT` column: a high major fault count means either memory
pressure or a cold cache. In the measured run `dockerd` (1396) and `mongod` (1096) led the
table — both page large binaries in from disk.

## 5. Page cache: the RAM that "isn't free" actually is

Measured with `pagecache_demo 64`, using `mincore(2)`:

| Stage | Pages resident in cache |
|---|---|
| Right after writing a 64MB file | 16384 / 16384 (100%) |
| After `posix_fadvise(DONTNEED)` | **0 / 16384 (0%)** |
| After reading it end to end | 16384 / 16384 (100%) |

What this teaches:
- **Writing also fills the cache.** After writing a file its contents are already in RAM, so a
  benchmark that writes then immediately reads never measures the disk at all. A classic
  benchmarking mistake.
- `mincore()` is the clean way to measure cache residency without root.
  Writing to `/proc/sys/vm/drop_caches` requires root and affects the **whole system**;
  `posix_fadvise` drops only the target file, which is what you want for a measurement.
- The "Cached" figure in `free` is not lost memory — the kernel reclaims it under pressure.
  The number to watch is `MemAvailable`, not `MemFree`. (Measured: Free 6.6G, Available 8.2G.)

## 6. To the kernel, a thread is a process

Under `/proc/<pid>/task/<tid>/` each thread has **its own `stat` and `status`** — the layout is
identical to `/proc/<pid>/`. On Linux a thread is a *task* that shares an address space;
apart from the `clone()` flags there is no difference from a process.

Measured on `mongod` (`procmon threads 1922`): 32 threads, with names showing up truncated —
the **`comm` field is capped at 15 characters**, hence `TenantM.ckerNet`.

## 7. What the context-switch counters tell you

The two counters in `status` are directly useful for diagnosis:

- **`voluntary_ctxt_switches`** — the task gave up the CPU itself: waiting on I/O, on a lock,
  or sleeping. A high value means the work is **I/O-bound**.
- **`nonvoluntary_ctxt_switches`** — its time slice expired and the scheduler preempted it.
  A high value means the work is **CPU-bound** and contending for CPU.

In the measured run, `mongod`'s `TicketH.Monitor` thread showed 2067 voluntary / 3 involuntary:
almost entirely waiting. The `procmon` process itself showed 16 / 1 — short-lived, does its
work and exits.

## 8. `/proc/<pid>/maps` is the map of the address space

Each line is a VMA (virtual memory area). The last character of the permission column is the
important one: **`p` = private (copy-on-write), `s` = shared**.

The pattern observed in the binary's own map is typical of dynamic linking — four regions per
library:

```
r-xp   code (executable, not writable)
---p   guard / alignment gap, inaccessible
r--p   read-only data (protected after relocation)
rw-p   writable data (GOT, globals)
```

The `libc.so.6` code region is 1.5M and is **shared across every process** — with 100 processes
running there is still one physical copy. This is why summing RSS across processes overstates
real memory use; the honest metric is PSS, in `smaps`.

`[heap]` and anonymous regions have no backing file, so they count as `RssAnon`. The rest is
`RssFile`: their contents come from the page cache and can be dropped under pressure.

## 9. The `/proc` view depends on the PID namespace

Running the same binary two ways:

- `make docker-shell` → `procmon list` sees **2 processes**.
- `make docker-shell-hostpid` (`--pid=host`) → the same command sees **248 processes**.

The code didn't change; `/proc` did. What a container sees is a view **filtered by the kernel**
according to its PID namespace. This makes it concrete that isolation here is not
virtualisation but a visibility restriction inside the kernel.

---

## Open questions

- Measuring PSS via `smaps` / `smaps_rollup` — the correct way to account for shared memory.
- How do huge pages (`AnonHugePages`) change RSS and fault counts?
- The CFS vruntime fields in `/proc/<pid>/sched`, for going deeper on the scheduler.
- Is the fault-around window (`fault_around_bytes`) tunable, and what does it do to minflt?

## References

- `man 5 proc` — field-by-field reference, the authoritative source
- `man 2 mmap`, `man 2 mincore`, `man 2 posix_fadvise`
