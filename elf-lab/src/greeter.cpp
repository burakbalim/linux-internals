// Calls into libgreet. Which call happens first matters: with lazy binding a symbol
// is not resolved until the first call reaches it, which LD_DEBUG=bindings shows.
#include <cstdio>
#include <unistd.h>

extern "C" void greet(const char *name);
extern "C" int add(int a, int b);

int main()
{
    std::printf("  pid %d\n", static_cast<int>(::getpid()));

    std::printf("  before first call to greet()\n");
    greet("world");

    std::printf("  add(2, 3) = %d\n", add(2, 3));
    return 0;
}
