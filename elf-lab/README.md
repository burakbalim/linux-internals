# elf-lab

A small shared library, a program that calls it, and a script that walks through what
the ELF format and the dynamic linker actually do with them.

Not a tool — a set of experiments. Each step prints the command it runs, so anything
interesting can be re-run by hand and poked at.

## Running it

```bash
make experiments
```

Or build and step through manually:

```bash
make all
./scripts/run-experiments.sh
```

Needs `readelf`, `objdump` and `ldd` (Debian: `binutils`). The `evserver` image already
has them: `docker run --rm -v "$PWD":/work -w /work evserver-dev ./scripts/run-experiments.sh`

## What it covers

| Step | Question |
|---|---|
| 1 | What does the ELF header say, and why is a modern executable type `DYN`? |
| 2 | Sections vs segments — and where `/proc/<pid>/maps` regions come from |
| 3 | `NEEDED`, `RUNPATH`, and how libraries get found at startup |
| 4 | Why a call to a library function goes through the PLT and GOT |
| 5 | Lazy binding: proving a symbol resolves on first call, not at startup |
| 6 | `LD_BIND_NOW`, and the startup-time-for-hardening trade |
| 7 | `LD_PRELOAD` interposition: replacing a function without recompiling |
| 8 | Static linking, and why `minicontainer`'s memhog is built that way |

## Files

```
src/libgreet.cpp     the shared library: greet() and add()
src/greeter.cpp      calls them, printing before each call so ordering is visible
src/libpreload.cpp   an alternative greet(), for LD_PRELOAD
scripts/             run-experiments.sh
```

Findings and the output worth keeping: [NOTES.md](NOTES.md)
