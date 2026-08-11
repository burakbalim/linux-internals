FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        make \
        gdb \
        strace \
        linux-perf \
        procps \
        binutils \
        liburing-dev \
        less \
    && rm -rf /var/lib/apt/lists/*

# ltrace is not packaged for arm64 on bookworm. For library call tracing use
# LD_DEBUG=bindings instead, which needs no extra package:
#   LD_DEBUG=bindings ./build/evserver --help 2>&1 | head

WORKDIR /work
CMD ["/bin/bash"]
