# evserver

The same TCP echo server written three ways, so the differences between I/O models can
be measured rather than argued about.

| Backend | Model |
|---|---|
| `threads` | one blocking thread per connection — the baseline epoll replaced |
| `epoll` | single-threaded epoll, level-triggered |
| `epoll-et` | single-threaded epoll, edge-triggered — drains each socket until `EAGAIN` |

Shutdown goes through **`signalfd`**: `SIGINT` is blocked and delivered as a readable
descriptor sitting in `epoll` alongside the sockets, so stopping the server is part of
the normal event flow instead of an async handler.

## Build and run

```bash
make docker-shell     # builds the image, mounts the source, opens a shell
make all
./build/evserver --backend=epoll
```

In a second shell on the same container:

```bash
docker exec -it evserver-dev bash
./build/loadgen --threads=8 --seconds=5
```

Stop the server with Ctrl-C and it prints its counters.

## The measurement that matters

Throughput on a few busy connections is *not* where these models differ — the baseline
does fine there. The difference shows up with many **idle** connections, which is what
real servers actually hold:

```bash
./build/loadgen --threads=8 --idle=500 --seconds=5
```

Then, from a third shell, look at what the server costs:

```bash
procmon show <server pid>      # RSS, thread count
procmon threads <server pid>   # per-thread context switches
```

Under `epoll` an idle connection is one file descriptor. Under `threads` it is a whole
thread, with a stack and a scheduler entry. Numbers from an actual run are in
[NOTES.md](NOTES.md).

## Level-triggered vs edge-triggered

The distinction is the reason both epoll modes are here:

- **Level-triggered** reports a descriptor as ready *while* data remains buffered.
  Reading once per wakeup is correct; the next `epoll_wait` reports it again.
- **Edge-triggered** (`EPOLLET`) reports only when readiness *changes*. If you do not
  drain the socket until `EAGAIN`, the remaining bytes are never reported again and the
  connection hangs. The same applies to `accept()`.

Compare the `read() calls` and `reads returning EAGAIN` counters between the two modes
to see the trade being made.

## Options

```
evserver [--backend=threads|epoll|epoll-et] [--port=N] [--buffer=N] [--max-events=N]
loadgen  [--port=N] [--threads=N] [--idle=N] [--seconds=N] [--payload=N]
```

## Layout

```
include/ev/    socket.hpp (RAII fd), signals.hpp (signalfd), server.hpp, stats.hpp
src/           server_epoll.cpp, server_threads.cpp, socket.cpp, signals.cpp, main.cpp
bench/         loadgen.cpp
```
