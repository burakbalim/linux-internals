# Measurement Notes — Namespaces and cgroups

Everything below was observed while building this, on a privileged Docker container
(aarch64, Linux 6.x, cgroup v2). Three of the four sections are bugs that took real
debugging, which is where the understanding actually came from.

---

## 1. Isolation is visible as inode numbers

Namespaces are kernel objects, and `/proc/<pid>/ns/*` are symlinks to them. Two processes
share a namespace exactly when the ids match. Measured, host versus container:

| Namespace | Host | Container | |
|---|---|---|---|
| pid | `4026532817` | `4026532933` | replaced |
| mnt | `4026532814` | `4026532930` | replaced |
| net | `4026532818` | `4026532934` | replaced |
| uts | `4026532815` | `4026532931` | replaced |
| ipc | `4026532816` | `4026532932` | replaced |
| cgroup | `4026532929` | `4026532929` | **shared** |
| user | `4026531837` | `4026531837` | **shared** |

The two shared ones were not requested, and that is the point: isolation is not a mode a
process is in, it is a per-namespace choice. A "container" is just a process for which
some of these numbers differ. Sharing the user namespace is also why this needs root —
without `CLONE_NEWUSER` there is no unprivileged path to the other namespaces.

Inside the container `echo $$` prints **1**, and `/proc` lists 4 processes instead of the
host's. Same `/proc` reading code as in `procmon`, different answer: procfs renders what
the caller's namespace can see.

## 2. The pipe deadlock: clone() copies both ends

The child must not start before its cgroup limits exist, otherwise it can allocate past
the limit before the limit is applied. The barrier is a pipe: the child reads, the parent
closes the write end, the read returns EOF.

It hung forever. `clone()` without `CLONE_VM` copies the descriptor table, so **the child
holds its own copy of the write end too**. A pipe reports EOF only when *every* write end
is closed — the parent closing its copy is not enough while the child still holds one.

```cpp
::close(args->ready_write_fd);   // the child's own copy - without this, deadlock
while (::read(args->ready_read_fd, &byte, 1) < 0 && errno == EINTR) {}
```

The general rule: after fork/clone, close every descriptor you do not intend to use.
"The parent closed it" is not the same as "it is closed".

## 3. cgroup v2 forbids internal processes

Writing `+memory +pids` to `/sys/fs/cgroup/cgroup.subtree_control` failed with **EBUSY**,
and the container ran with no limits at all.

The rule: a cgroup may hold processes, or delegate controllers to its children, **but not
both**. Only the true system root is exempt. With `--cgroupns=private` the container gets
its own cgroup root — but that root is still an ordinary cgroup on the host, and it already
contained the shell we started from, so the rule applied.

The fix is what systemd and every real runtime do: move the existing processes into a leaf
cgroup first, leaving the root empty and free to delegate.

```
/sys/fs/cgroup/
├── init/                 <- existing processes moved here
├── cgroup.subtree_control = "+memory +pids"
└── minicontainer/
    └── mc-<pid>/         <- limits applied here
```

`cgroup.procs` also accepts exactly **one pid per write** — writing the list in one go
fails.

## 4. memory.max does not limit memory

This was the most interesting failure. With `--memory=64M` the process was OOM-killed, so
the limit looked like it worked — except it allocated **3.9 GB** first:

```
allocated 3880 MB
allocated 3890 MB
allocated 3900 MB
  cgroup OOM kills: 1
```

3.9 GB is not 64 MB. It is, however, almost exactly the machine's **4 GB of swap**.

`memory.max` caps resident memory. When a process exceeds it, the kernel does the same
thing it does under any memory pressure: it reclaims pages by writing them to swap. The
cgroup stays within its limit the whole time, and the process keeps allocating until swap
itself runs out.

cgroup v2 accounts swap separately, so the limit only means what it appears to mean once
both are set:

```cpp
write_file(path + "/memory.max", "67108864");
write_file(path + "/memory.swap.max", "0");   // without this, the limit is swap-sized
```

After the fix, the same command dies where it should:

```
allocated   50 MB
allocated   60 MB
  cgroup OOM kills: 1  (the memory limit was enforced)
  container killed by signal 9 (Killed)
```

Worth remembering when a container "respects its memory limit" but the host is thrashing.

## 5. pids.max is the fork bomb defence

Without a limit, a shell loop starting 40 background processes gets all 40 (44 total in the
namespace). With `--pids=10`:

```
/bin/sh: can't fork: Resource temporarily unavailable
```

`fork()` returns `EAGAIN` once the cgroup is at its limit. The limit was tight enough that
the shell could not even fork to run `ls` — which is precisely the containment wanted.

## 6. pivot_root needs the mount namespace made private first

`pivot_root` fails unless the new root is a mount point, and unless mount propagation has
been turned off:

```cpp
mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr);  // stop propagating to host
mount(rootfs, rootfs, nullptr, MS_BIND | MS_REC, nullptr);   // make it a mount point
pivot_root(rootfs, put_old);
umount2("/old_root", MNT_DETACH);                            // host now unreachable
```

Without `MS_REC|MS_PRIVATE` the new mounts propagate back to the host — a new mount
namespace starts as a *copy* of the parent's, with propagation still shared, not as an
empty one.

The final `umount2` is what separates this from `chroot`: until the old root is detached it
is still mounted and still reachable. `chroot` only moves a pointer; `pivot_root` plus
detach removes the path entirely.

Also: `pivot_root` has no glibc wrapper, so it goes through `syscall(SYS_pivot_root, ...)`.

---

## Open questions

- User namespaces (`CLONE_NEWUSER`) would make this runnable without root, by mapping
  container root to an unprivileged host uid. That is how rootless Podman works.
- Networking: the container currently gets an empty netns. Real isolation with
  connectivity needs a veth pair, a bridge and NAT.
- `memory.high` throttles instead of killing — a gentler limit worth comparing.
- Copy the `procmon` binary into the rootfs and run it inside, to see week-2 tooling report
  on a namespace it cannot see out of.

## Reproducing

```bash
make docker-shell     # --privileged --cgroupns=private
make all && make rootfs
./build/minicontainer run --rootfs=$(pwd)/rootfs --memory=64M -- /bin/memhog 10
```

Inspect the live cgroup from another shell (`docker exec -it minicontainer-dev bash`):

```bash
cat /sys/fs/cgroup/minicontainer/mc-*/memory.max
cat /sys/fs/cgroup/minicontainer/mc-*/memory.current
```
