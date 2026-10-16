#!/bin/bash
# Builds the Ubuntu 24.04 system KeplerOS runs on, and compiles the Kepler
# browser (github.com/lenodrips-create/kepler) inside it.
#
#   sudo linux/build-rootfs.sh WORKDIR [KEPLER_SRC]
#
# WORKDIR/rootfs ends up holding the finished system (without the KeplerOS
# shell itself, which linux/build-iso.sh copies in).
set -euo pipefail

WORK=${1:?usage: build-rootfs.sh WORKDIR [KEPLER_SRC]}
KEPLER_SRC=${2:-}
ROOT=$WORK/rootfs
MIRROR=${MIRROR:-http://archive.ubuntu.com/ubuntu}
SUITE=noble

mkdir -p "$WORK"

chroot_run() { chroot "$ROOT" /usr/bin/env DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 "$@"; }

mount_all() {
    mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
    mountpoint -q "$ROOT/sys" || mount -t sysfs sys "$ROOT/sys"
    mountpoint -q "$ROOT/dev" || mount --bind /dev "$ROOT/dev"
    mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
}
umount_all() {
    for m in dev/pts dev sys proc; do
        mountpoint -q "$ROOT/$m" && umount -l "$ROOT/$m" || true
    done
}
trap umount_all EXIT

if [ ! -x "$ROOT/usr/bin/apt-get" ]; then
    debootstrap --variant=minbase --components=main,universe "$SUITE" "$ROOT" "$MIRROR"
fi

cat > "$ROOT/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main universe
deb $MIRROR $SUITE-updates main universe
deb $MIRROR $SUITE-security main universe
EOF
# keep the image small
cat > "$ROOT/etc/dpkg/dpkg.cfg.d/01-nodoc" <<'EOF'
path-exclude /usr/share/doc/*
path-include /usr/share/doc/*/copyright
path-exclude /usr/share/man/*
path-exclude /usr/share/info/*
path-exclude /usr/share/locale/*
path-include /usr/share/locale/locale.alias
path-exclude /usr/share/lintian/*
EOF
# no services may start inside the chroot
printf '#!/bin/sh\nexit 101\n' > "$ROOT/usr/sbin/policy-rc.d"
chmod +x "$ROOT/usr/sbin/policy-rc.d"
echo kepleros > "$ROOT/etc/hostname"

mount_all
chroot_run apt-get update

RUNTIME=(
    # boot
    linux-image-generic casper initramfs-tools systemd systemd-sysv systemd-resolved udev dbus dbus-user-session
    kmod sudo ca-certificates iproute2 locales tzdata plymouth systemd-zram-generator
    # graphics
    xserver-xorg-core xserver-xorg-video-fbdev xserver-xorg-video-vesa xserver-xorg-video-vmware
    xserver-xorg-video-qxl xserver-xorg-video-amdgpu xserver-xorg-video-intel xserver-xorg-input-libinput
    xinit x11-xserver-utils libgl1-mesa-dri libegl1 libgles2
    # the Linux Terminal
    xterm
    # what the KeplerOS shell links against
    libx11-6 libxext6 libxcomposite1 libxdamage1 libxfixes3 libxcursor1 libxrandr2
    # Kepler's runtime
    libqt6webenginewidgets6 libqt6webenginecore6-bin libqt6svg6 qt6-qpa-plugins qt6-image-formats-plugins
    qt6-wayland
    # fonts and sound for the web
    fonts-dejavu-core fonts-liberation2 fonts-noto-color-emoji fonts-noto-core
    pipewire pipewire-pulse wireplumber
)
chroot_run apt-get install -y --no-install-recommends "${RUNTIME[@]}"

if [ -n "$KEPLER_SRC" ] && [ ! -x "$ROOT/usr/bin/Kepler" ]; then
    BUILD=(build-essential cmake qt6-base-dev qt6-webengine-dev qt6-svg-dev libgl-dev)
    chroot_run apt-get install -y --no-install-recommends "${BUILD[@]}"
    rm -rf "$ROOT/tmp/kepler"
    cp -a "$KEPLER_SRC" "$ROOT/tmp/kepler"
    rm -rf "$ROOT/tmp/kepler/build" "$ROOT/tmp/kepler/.git"
    chroot_run sh -c 'cd /tmp/kepler && cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        && cmake --build build -j"$(nproc)" && cmake --install build'
    rm -rf "$ROOT/tmp/kepler"
    chroot_run apt-get purge -y --auto-remove "${BUILD[@]}"
    # and the compiler they pulled in (cpp stays: X's xrdb needs it)
    chroot_run apt-get purge -y --auto-remove gcc gcc-13 binutils libc6-dev libgcc-13-dev linux-libc-dev dpkg-dev make patch
    # purging the -dev packages can take runtime libraries with it
    chroot_run apt-get install -y --no-install-recommends "${RUNTIME[@]}"
fi

# the live user: signs in on tty1 by itself and starts X with the KeplerOS shell
if ! chroot_run id kepler >/dev/null 2>&1; then
    chroot_run useradd -m -s /bin/bash -G sudo,video,audio,input,render kepler
    echo 'kepler:kepler' | chroot_run chpasswd
fi
echo 'kepler ALL=(ALL) NOPASSWD: ALL' > "$ROOT/etc/sudoers.d/kepler"
chmod 440 "$ROOT/etc/sudoers.d/kepler"
printf 'export USERNAME="kepler"\nexport USERFULLNAME="Kepler"\nexport HOST="kepleros"\nexport BUILD_SYSTEM="Ubuntu"\nexport FLAVOUR="KeplerOS"\n' \
    > "$ROOT/etc/casper.conf"

mkdir -p "$ROOT/etc/systemd/system/getty@tty1.service.d"
cat > "$ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin kepler --noclear --skip-login --nonewline --noissue %I $TERM
TTYVTDisallocate=no
EOF
cat > "$ROOT/home/kepler/.bash_profile" <<'EOF'
if [ -z "$DISPLAY" ] && [ "$(tty)" = /dev/tty1 ]; then
    exec startx /usr/lib/kepleros/session -- -nolisten tcp -nocursor vt1 >/tmp/xsession.log 2>&1
fi
EOF
chroot_run chown kepler:kepler /home/kepler/.bash_profile

# wired networking by DHCP on any ethernet card
mkdir -p "$ROOT/etc/systemd/network"
cat > "$ROOT/etc/systemd/network/20-wired.network" <<'EOF'
[Match]
Type=ether

[Network]
DHCP=yes
EOF
chroot_run systemctl enable systemd-networkd systemd-resolved
ln -sf /run/systemd/resolve/stub-resolv.conf "$ROOT/etc/resolv.conf"

# quiet console: no messages over the boot
echo 'kernel.printk = 0 0 0 0' > "$ROOT/etc/sysctl.d/20-quiet.conf"

chroot_run apt-get clean
rm -rf "$ROOT"/var/lib/apt/lists/* "$ROOT/usr/sbin/policy-rc.d"
echo "rootfs ready: $(du -sxh "$ROOT" | cut -f1)"
