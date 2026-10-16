#!/usr/bin/env python3
"""Boot KeplerOS in a headless QEMU, drive it through QMP and take screenshots.

Usage:
    tools/qemu_drive.py SCRIPT [--kernel build/kepler.elf] [--outdir shots]

SCRIPT is a list of commands separated by ';' or newlines:

    wait MS              sleep for MS milliseconds
    waitlog TEXT         wait until the serial log contains TEXT
    shot NAME            save a PNG screenshot as OUTDIR/NAME.png
    key COMBO            press a key, e.g. "ret", "meta_l", "ctrl-s", "shift-a"
    type TEXT            type text (use \\n for Enter)
    move X Y             move the mouse pointer to absolute X,Y
    rel DX DY            move the mouse pointer relatively
    click [right]        click at the current position
    dclick               double-click at the current position
    down / up            press / release the left button (for drags)
    drag X Y             press, move to X,Y in small steps, release
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

from PIL import Image

SHIFTED = {
    '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0',
    '_': 'minus', '+': 'equal', '{': 'bracket_left', '}': 'bracket_right', '|': 'backslash',
    ':': 'semicolon', '"': 'apostrophe', '~': 'grave_accent', '<': 'comma', '>': 'dot', '?': 'slash',
}
PLAIN = {
    ' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right',
    '\\': 'backslash', ';': 'semicolon', "'": 'apostrophe', '`': 'grave_accent', ',': 'comma', '.': 'dot',
    '/': 'slash', '\t': 'tab',
}


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(200):
            try:
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.05)
        self.f = self.sock.makefile('rw')
        self._read()
        self.cmd('qmp_capabilities')

    def _read(self):
        while True:
            msg = json.loads(self.f.readline())
            if 'event' not in msg:
                return msg

    def cmd(self, name, **args):
        self.f.write(json.dumps({'execute': name, 'arguments': args}) + '\n')
        self.f.flush()
        r = self._read()
        if 'error' in r:
            raise RuntimeError(f'{name}: {r["error"]}')
        return r.get('return')


class Driver:
    def __init__(self, qmp, outdir, log):
        self.q = qmp
        self.outdir = outdir
        self.log = log
        self.x = self.y = 0
        self.buttons = 0

    def _rel(self, dx, dy):
        # PS/2 carries at most +-255 per packet; QEMU splits bigger moves itself,
        # but keeping the steps small keeps its queue from overflowing.
        while dx or dy:
            sx = max(-120, min(120, dx))
            sy = max(-120, min(120, dy))
            self.q.cmd('input-send-event', events=[
                {'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': sy}},
            ])
            dx -= sx
            dy -= sy
            time.sleep(0.012)

    def home(self):
        self._rel(-3000, -3000)
        time.sleep(0.15)
        self.x = self.y = 0

    def move(self, x, y):
        self.home()
        self._rel(x, y)
        self.x, self.y = x, y
        time.sleep(0.15)

    def button(self, down, which='left'):
        self.q.cmd('input-send-event', events=[{'type': 'btn', 'data': {'down': down, 'button': which}}])
        time.sleep(0.06)

    def click(self, which='left'):
        self.button(True, which)
        self.button(False, which)
        time.sleep(0.12)

    def key(self, combo):
        keys = [{'type': 'qcode', 'data': k} for k in combo.split('-')] if combo != '-' else [{'type': 'qcode', 'data': 'minus'}]
        self.q.cmd('send-key', keys=keys, **{'hold-time': 40})
        time.sleep(0.09)

    def type(self, text):
        for ch in text:
            if ch.isalpha() and ch.isupper():
                self.key('shift-' + ch.lower())
            elif ch.isalnum():
                self.key(ch)
            elif ch in SHIFTED:
                self.key('shift-' + SHIFTED[ch])
            elif ch in PLAIN:
                self.key(PLAIN[ch])

    def shot(self, name):
        ppm = os.path.join(tempfile.gettempdir(), f'kepler-{os.getpid()}.ppm')
        self.q.cmd('screendump', filename=ppm)
        time.sleep(0.2)
        out = os.path.join(self.outdir, name + '.png')
        Image.open(ppm).save(out)
        os.unlink(ppm)
        print('saved', out)

    def waitlog(self, text, timeout=60):
        end = time.time() + timeout
        while time.time() < end:
            try:
                with open(self.log, errors='replace') as f:
                    if text in f.read():
                        return
            except FileNotFoundError:
                pass
            time.sleep(0.1)
        raise TimeoutError(f'serial log never showed {text!r}')

    def run(self, script):
        for raw in script.replace(';', '\n').splitlines():
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            op, _, arg = line.partition(' ')
            if op == 'wait':
                time.sleep(int(arg) / 1000)
            elif op == 'waitlog':
                self.waitlog(arg)
            elif op == 'shot':
                self.shot(arg)
            elif op == 'key':
                self.key(arg)
            elif op == 'type':
                self.type(arg.replace('\\n', '\n'))
            elif op == 'move':
                x, y = map(int, arg.split())
                self.move(x, y)
            elif op == 'rel':
                dx, dy = map(int, arg.split())
                self._rel(dx, dy)
                self.x += dx
                self.y += dy
                time.sleep(0.1)
            elif op == 'click':
                self.click(arg or 'left')
            elif op == 'dclick':
                self.button(True)
                self.button(False)
                self.button(True)
                self.button(False)
                time.sleep(0.15)
            elif op == 'down':
                self.button(True)
            elif op == 'up':
                self.button(False)
            elif op == 'drag':
                tx, ty = map(int, arg.split())
                self.button(True)
                steps = 12
                px, py = self.x, self.y
                for i in range(1, steps + 1):
                    nx = px + (tx - px) * i // steps
                    ny = py + (ty - py) * i // steps
                    self._rel(nx - self.x, ny - self.y)
                    self.x, self.y = nx, ny
                    time.sleep(0.03)
                time.sleep(0.1)
                self.button(False)
                time.sleep(0.1)
            else:
                raise ValueError(f'unknown command: {line}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('script')
    ap.add_argument('--kernel', default='build/kepler.elf')
    ap.add_argument('--outdir', default='shots')
    ap.add_argument('--mem', default='256M')
    ap.add_argument('--iso', help='boot this CD image instead of the kernel')
    ap.add_argument('--linux', action='store_true', help='the image is the 64-bit Linux-based KeplerOS')
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='kepler-qemu-')
    sock = os.path.join(tmp, 'qmp.sock')
    log = os.path.join(tmp, 'serial.log')
    machine = ['qemu-system-x86_64', '-smp', '4', '-cpu', 'max'] if args.linux else ['qemu-system-i386']
    proc = subprocess.Popen([
        *machine, *(['-cdrom', args.iso] if args.iso else ['-kernel', args.kernel]), '-m', args.mem, '-vga', 'std',
        '-display', 'none', '-serial', f'file:{log}', '-qmp', f'unix:{sock},server,nowait',
        '-rtc', 'base=localtime', '-no-reboot', '-nic', 'user,model=e1000',
    ])
    try:
        d = Driver(Qmp(sock), args.outdir, log)
        script = open(args.script).read() if os.path.exists(args.script) else args.script
        d.run(script)
    finally:
        proc.terminate()
        proc.wait()
        if os.path.exists(log):
            sys.stdout.write(open(log, errors='replace').read()[-3000:])


if __name__ == '__main__':
    main()
