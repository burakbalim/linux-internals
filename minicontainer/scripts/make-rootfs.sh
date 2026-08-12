#!/bin/sh
# Builds a minimal root filesystem from busybox, which is statically linked and so
# needs no libraries inside the container.
set -e

ROOTFS=${1:-rootfs}

mkdir -p "$ROOTFS"/bin "$ROOTFS"/proc "$ROOTFS"/sys "$ROOTFS"/dev "$ROOTFS"/tmp

cp /bin/busybox "$ROOTFS/bin/"

# busybox decides which tool to be from the name it was invoked as, so every command
# is a symlink back to the same binary. Skip "busybox" itself: it appears in the applet
# list, and linking it would replace the real binary with a symlink to itself.
for applet in $(/bin/busybox --list); do
    [ "$applet" = "busybox" ] && continue
    ln -sf busybox "$ROOTFS/bin/$applet"
done

[ -f build/memhog ] && cp build/memhog "$ROOTFS/bin/"

ln -sfn bin "$ROOTFS/sbin" 2>/dev/null || true

echo "rootfs ready: $ROOTFS ($(du -sh "$ROOTFS" | cut -f1))"
echo "try: ./build/minicontainer run --rootfs=\$(pwd)/$ROOTFS -- /bin/sh"
