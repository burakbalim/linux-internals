#!/bin/bash
# Walks through what the ELF format and the dynamic linker actually do, using the
# small library and program built by the Makefile. Each step prints the command it
# runs so the output can be reproduced by hand.
set -u

B=build
FILTER='grep -E "before first call|binding file.*to .*libgreet|greet: hello|add\\(2" | sed "s/^ *[0-9]*:\\t/  [ld] /"'
hr() { printf '\n\033[1m%s\033[0m\n%s\n' "$1" "$(printf '=%.0s' $(seq 1 ${#1}))"; }
run() { printf '\n$ %s\n' "$*"; eval "$@"; }

hr "1. An ELF file starts with a header that says what it is"
run "readelf -h $B/greeter | head -12"
echo "
  Type DYN, not EXEC: modern executables are position-independent, so the kernel
  can load them at a random base address (ASLR). The entry point is an offset."

hr "2. Sections are for the linker, segments are for the loader"
run "readelf -S $B/greeter | grep -E 'Name|\.text|\.data|\.bss|\.rodata|\.got|\.plt' | head -10"
run "readelf -l $B/greeter | grep -A1 -E 'Type|LOAD' | head -14"
echo "
  The same bytes are described twice. Sections (.text, .data) are how the linker
  organises the file; segments (LOAD) are what the kernel maps into memory. This is
  where the r-xp / r--p / rw-p regions seen in /proc/<pid>/maps come from."

hr "3. What the program needs at runtime"
run "readelf -d $B/greeter | head -12"
echo "
  NEEDED entries are recorded by name only, not by path - resolution happens at
  startup. RUNPATH here is \$ORIGIN, meaning 'next to the executable'."

run "ldd $B/greeter"

hr "4. Calls into a shared library go through the PLT"
run "objdump -d $B/greeter --section=.plt 2>/dev/null | head -14"
run "readelf -r $B/greeter | sed -n '/rela.plt/,\$p' | head -8"
echo "
  A call to greet() does not jump to the library. It jumps to a stub in .plt, which
  reads an address from .got. That indirection is what makes relocation possible."

hr "5. Lazy binding: symbols resolve on first call, not at startup"
echo "
  Interleaving the linker's trace with the program's own output shows the ordering.
  stdbuf -o0 matters here: piped stdout is block-buffered while the linker writes to
  stderr unbuffered, so without it the program's output appears last and the ordering
  looks exactly backwards."
run "stdbuf -o0 env LD_DEBUG=bindings $B/greeter 2>&1 | $FILTER"
echo "
  greet is bound after 'before first call' is printed, and add is not bound until
  add() is actually reached. The first call went to a PLT stub, which jumped into the
  dynamic linker, which resolved the symbol and patched the GOT. Later calls go
  straight through - the cost is paid once, per symbol, only if it is ever used."

hr "6. LD_BIND_NOW resolves everything up front"
run "stdbuf -o0 env LD_BIND_NOW=1 LD_DEBUG=bindings $B/greeter 2>&1 | $FILTER"
echo "
  Both symbols are now resolved before main() runs at all."
run "LD_DEBUG=statistics $B/greeter 2>&1 >/dev/null | grep -E 'number of relocations:' | head -2"
run "LD_BIND_NOW=1 LD_DEBUG=statistics $B/greeter 2>&1 >/dev/null | grep -E 'number of relocations:' | head -2"
echo "
  Eager binding does more work at startup (82 -> 110 relocations here) but removes the
  first-call cost and lets the GOT be made read-only afterwards (full RELRO), which
  closes off a common exploitation target. Startup time traded for safety."

hr "7. Interposition: replacing a function without recompiling"
run "$B/greeter"
run "LD_PRELOAD=\$PWD/$B/libpreload.so $B/greeter"
echo "
  Same binary, different greet(). The dynamic linker searches LD_PRELOAD first, so
  the first definition found wins. This is how sanitizers, profilers and fakeroot
  work - and why LD_PRELOAD is a security-sensitive variable."

hr "8. Static linking: no dynamic linker at all"
run "ls -l $B/greeter $B/greeter-static | awk '{print \$5, \$9}'"
run "readelf -d $B/greeter-static 2>&1 | head -3"
run "ldd $B/greeter-static 2>&1 | head -2"
echo "
  Much larger, but self-contained: it runs in an empty rootfs, which is exactly why
  minicontainer's memhog is built static. The trade is that a libc security fix
  requires relinking rather than just updating the shared library."
echo
