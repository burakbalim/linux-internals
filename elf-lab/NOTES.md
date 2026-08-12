# Measurement Notes — ELF and Dynamic Linking

Observed on aarch64 Debian bookworm, gcc 12, glibc 2.36. Everything below is output from
`scripts/run-experiments.sh`.

---

## 1. Lazy binding, proven rather than asserted

The claim is that a symbol from a shared library is not resolved when the program starts,
but on the first call that reaches it. Interleaving the linker's trace with the program's
own output shows exactly that:

```
  before first call to greet()
  [ld] binding file greeter to libgreet.so: normal symbol `greet'
  greet: hello, world
  [ld] binding file greeter to libgreet.so: normal symbol `add'
  add(2, 3) = 5
```

`greet` is bound **after** the program printed "before first call". `add` is not bound at
all until `add()` is reached. With `LD_BIND_NOW=1` the same run inverts:

```
  [ld] binding ... `add'
  [ld] binding ... `greet'
  before first call to greet()
  greet: hello, world
```

Both resolved before `main()` runs.

Mechanically: the first call jumps to a stub in `.plt`, which jumps through `.got`, which
initially points back into the dynamic linker. The linker resolves the symbol, **patches
the GOT entry**, and jumps to the real function. Every later call reads the patched entry
and goes straight there. The cost is paid once per symbol, and only for symbols actually
used — which is why a program linking a huge library does not pay for all of it.

`LD_BIND_NOW` did 82 → **110** relocations at startup. The gain is that the GOT can then
be made read-only (full RELRO), removing a favourite exploitation target: a writable GOT
is a table of function pointers.

## 2. The buffering trap that inverted the result

The first run of that experiment showed the bindings appearing **before** the program's
output — the exact opposite. Nothing was wrong with the linker; the pipeline was lying.

Piped stdout is **block-buffered**, so the program's `printf` output sat in a buffer until
exit. The linker writes to stderr **unbuffered**. Through a pipe, all the linker lines
appear first regardless of when they happened.

`stdbuf -o0` makes stdout unbuffered and the true ordering appears.

Worth internalising beyond this experiment: **any time stdout and stderr are merged through
a pipe, the interleaving is not evidence of ordering.** The same trap ruins log timestamps
and CI output routinely.

## 3. Sections are for the linker, segments are for the kernel

`readelf -S` lists `.text`, `.rodata`, `.got`, `.data`, `.bss`. `readelf -l` lists two
`LOAD` segments:

```
LOAD  0x0 ... 0xa64   R E     <- code + read-only data
LOAD  0xfda8 ... 0x2b8 RW     <- writable data
```

The same bytes described twice, for two different audiences. The linker thinks in sections;
the kernel maps segments and does not care about section names at all.

This is where the four-region-per-library pattern seen in `procmon maps` comes from — the
`r-xp` / `r--p` / `rw-p` regions are these segments, mapped in.

## 4. Type is DYN, not EXEC

```
Type:  DYN (Position-Independent Executable file)
Entry point address:  0x800
```

A modern executable is built as a position-independent shared object, so the kernel can
load it at a random base address. That is what makes ASLR apply to the executable itself
and not just to libraries. The entry point is an offset, not an absolute address — which
is why addresses in `/proc/<pid>/maps` differ between runs.

## 5. Dependencies are recorded by name, not path

```
(NEEDED)   Shared library: [libgreet.so]
(NEEDED)   Shared library: [libc.so.6]
(RUNPATH)  Library runpath: [$ORIGIN]
```

The binary records *names*. Resolution happens at every startup, through `RUNPATH`,
`LD_LIBRARY_PATH`, and the `ld.so.cache`. `$ORIGIN` means "the directory the executable is
in", which is how a program ships with its own libraries without absolute paths.

The consequence: **the file that runs tomorrow may not be the file that ran today**. That
is the point of shared libraries — a libc security fix reaches every program without
relinking — and also the reason a working binary can break when its environment changes.

## 6. Interposition needs no recompilation

```
$ ./greeter
  greet: hello, world

$ LD_PRELOAD=$PWD/libpreload.so ./greeter
  [interposed] greet was hijacked, name=world
```

Same binary, unmodified. `LD_PRELOAD` libraries are searched first, and the first
definition found wins, so a matching symbol shadows the real one.

This is the mechanism behind sanitizers, allocator replacements (jemalloc, tcmalloc),
`fakeroot` and most `malloc` profilers. It is also why `LD_PRELOAD` is ignored for setuid
binaries — otherwise it would be a trivial privilege escalation.

## 7. Static linking: 75 KB → 710 KB

```
75152   greeter          (dynamic)
710640  greeter-static   (static)
$ ldd greeter-static
        not a dynamic executable
```

Nearly 10× larger, but it needs nothing at runtime — no linker, no libc on disk. That is
why `minicontainer`'s `memhog` is static: it runs inside a rootfs containing only busybox,
where a dynamic binary would fail to start.

The trade is the flip side of section 5: a glibc security fix requires relinking and
redeploying every static binary, rather than updating one shared library.

---

## Open questions

- `ld.so.cache` and `ldconfig`: how much startup time does the cache actually save?
- Symbol versioning (`GLIBC_2.17` in the trace) — how two versions of the same symbol
  coexist in one library.
- `-fno-plt` and how it changes the call sequence.
- `dlopen`/`dlsym` at runtime versus link-time resolution.

## Reproducing

```bash
make experiments
```
