#!/bin/bash
# Puts the KeplerOS shell into the Ubuntu system from build-rootfs.sh and
# makes a bootable live image (BIOS and UEFI).
#
#   sudo linux/build-iso.sh WORKDIR OUTPUT.iso
set -euo pipefail

WORK=${1:?usage: build-iso.sh WORKDIR OUTPUT.iso}
OUT=${2:?usage: build-iso.sh WORKDIR OUTPUT.iso}
ROOT=$WORK/rootfs
ISO=$WORK/iso
HERE=$(cd "$(dirname "$0")/.." && pwd)

[ -x "$ROOT/usr/bin/Kepler" ] || { echo "run linux/build-rootfs.sh first" >&2; exit 1; }
make -C "$HERE" linux

# ---- the shell and the session it runs in --------------------------------------------------
install -D -m755 "$HERE/build/linux/kepler-shell" "$ROOT/usr/lib/kepleros/kepler-shell"
install -D -m755 "$HERE/linux/session.sh" "$ROOT/usr/lib/kepleros/session"
cat > "$ROOT/home/kepler/.bash_profile" <<'EOF'
if [ -z "$DISPLAY" ] && [ "$(tty)" = /dev/tty1 ]; then
    exec startx /usr/lib/kepleros/session -- -nolisten tcp -keeptty vt1 >/tmp/xsession.log 2>&1
fi
EOF
chroot "$ROOT" chown kepler:kepler /home/kepler/.bash_profile
echo 'LANG=C.UTF-8' > "$ROOT/etc/default/locale"
# Chromium's sandbox needs user namespaces, which Ubuntu limits by default
echo 'kernel.apparmor_restrict_unprivileged_userns = 0' > "$ROOT/etc/sysctl.d/30-kepler-sandbox.conf"
# no login prompt or boot messages on the console
chroot "$ROOT" systemctl mask casper-md5check.service >/dev/null 2>&1 || true
: > "$ROOT/etc/issue"
: > "$ROOT/etc/motd"
rm -f "$ROOT"/etc/update-motd.d/* 2>/dev/null || true
touch "$ROOT/home/kepler/.hushlogin"

# ---- boot files ---------------------------------------------------------------------------
for m in proc sys dev; do mountpoint -q "$ROOT/$m" || mount --bind "/$m" "$ROOT/$m"; done
chroot "$ROOT" update-initramfs -u >/dev/null
for m in dev sys proc; do umount -l "$ROOT/$m"; done

rm -rf "$ISO"
mkdir -p "$ISO/casper" "$ISO/boot/grub" "$ISO/.disk"
cp -L "$ROOT/boot/vmlinuz" "$ISO/casper/vmlinuz"
cp -L "$ROOT/boot/initrd.img" "$ISO/casper/initrd"
echo "KeplerOS live" > "$ISO/.disk/info"

mksquashfs "$ROOT" "$ISO/casper/filesystem.squashfs" -comp xz -b 1M -noappend -no-progress \
    -wildcards -e 'boot/*' 'proc/*' 'sys/*' 'dev/*' 'run/*' 'tmp/*' 'var/cache/apt/*' 'var/log/*' \
    'home/kepler/.cache' 'home/kepler/.local' 'home/kepler/.config' 'home/kepler/.pki' 'etc/resolv.conf.test' 
printf '%s' "$(du -sxb "$ROOT" | cut -f1)" > "$ISO/casper/filesystem.size"

cat > "$ISO/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set timeout_style=hidden
menuentry "KeplerOS" {
    linux /casper/vmlinuz boot=casper noprompt quiet splash loglevel=0 systemd.show_status=false rd.udev.log_level=0 vt.global_cursor_default=0 ---
    initrd /casper/initrd
}
EOF

grub-mkrescue -o "$OUT" "$ISO" -- -volid KEPLEROS 2>&1 | tail -1
echo "image: $OUT ($(du -h "$OUT" | cut -f1))"
