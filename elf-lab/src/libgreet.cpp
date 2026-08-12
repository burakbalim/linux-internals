// A shared library with two functions, so symbol resolution has something to resolve.
#include <cstdio>

extern "C" void greet(const char *name)
{
    std::printf("  greet: hello, %s\n", name);
}

extern "C" int add(int a, int b)
{
    return a + b;
}
