FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        make \
        gdb \
        strace \
        procps \
        less \
        vim-tiny \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
