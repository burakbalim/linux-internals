// Interposition: defining greet() here and loading this library first makes every
// call resolve to this copy instead of the one in libgreet.so. The program is not
// recompiled or relinked - the dynamic linker simply finds this symbol earlier.
#include <cstdio>

extern "C" void greet(const char *name)
{
    std::printf("  [interposed] greet was hijacked, name=%s\n", name);
}
