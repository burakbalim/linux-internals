# minicontainer

A container runtime reduced to the parts that actually do the work: namespaces for
isolation, cgroups for limits, `pivot_root` for the filesystem. Around 400 lines of C++.

There is no image format, no registry, no networking setup — deliberately. The goal is to
show that "container" is not a kind of virtual machine but a normal process the kernel has
been told to show a narrower view of the system.

## What it does

```bash
minicontainer run [options] -- COMMAND [ARGS...]
minicontainer ns [pid]        # show a process's namespace ids
```

| Option | Effect |
|---|---|
| `--rootfs=DIR` | `pivot_root` into DIR |
| `--hostname=NAME` | hostname inside the container (UTS namespace) |
| `--memory=SIZE` | memory limit, e.g. `64M` (cgroup v2) |
| `--pids=N` | maximum number of processes (cgroup v2) |
| `--share-net` | keep the host network namespace (isolated by default) |

## Running it

Namespaces and cgroups need real privileges, and a writable cgroup2 root:

```bash
make docker-shell     # runs with --privileged --cgroupns=private
make all
make rootfs           # builds a ~1 MB busybox rootfs
```

Then:

```bash
./build/minicontainer run --rootfs=$(pwd)/rootfs --hostname=demo -- /bin/sh
```

Inside that shell: `hostname` is `demo`, `echo $$` prints **1**, `ls /proc` shows only the
container's own processes, and `/` is the busybox rootfs — not the host filesystem.

## The three mechanisms

**Namespaces** decide *what a process can see*. `clone()` is called with `CLONE_NEWPID`,
`CLONE_NEWNS`, `CLONE_NEWUTS`, `CLONE_NEWIPC` and `CLONE_NEWNET`, and the child gets fresh
instances of each. Compare the ids to prove it:

```bash
./build/minicontainer ns          # host
# then the same inside a container - pid/mnt/net/uts/ipc differ, user/cgroup match
```

**cgroups** decide *how much a process can use*. Limits are plain text files under
`/sys/fs/cgroup`. Two details cost real debugging time, both written up in
[NOTES.md](NOTES.md): controllers must be delegated through `cgroup.subtree_control`, and
`memory.max` alone does not bound anything because the pages simply go to swap.

**pivot_root** decides *what filesystem a process sees as `/`*. Unlike `chroot` it detaches
the old root entirely, so the host filesystem becomes unreachable rather than merely
inconvenient to reach.

## Seeing the limits work

```bash
# memory: allocates 10 MB at a time until the cgroup kills it
./build/minicontainer run --rootfs=$(pwd)/rootfs --memory=64M -- /bin/memhog 10

# pids: the fork bomb defence
./build/minicontainer run --rootfs=$(pwd)/rootfs --pids=10 -- /bin/sh -c \
  'i=1; while [ $i -le 40 ]; do sleep 3 & i=$((i+1)); done'
```

## Layout

```
include/mc/    util.hpp, cgroup.hpp (cgroup v2 RAII), container.hpp
src/           container.cpp (clone + pivot_root), cgroup.cpp, util.cpp, main.cpp
demo/          memhog.cpp - allocates until killed, statically linked for the rootfs
scripts/       make-rootfs.sh - minimal busybox root filesystem
```
