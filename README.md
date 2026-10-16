kepleros kernel
_________________
this is a newly based os made by Lennon Helman and with the help of claude code

![KeplerOS desktop](docs/screenshots/desktop.png)

KeplerOS is a small 32-bit x86 operating system written in C. It boots
straight into a graphical desktop styled after macOS Big Sur, with the
green-globe photo in `assets/stockwallpaper` as its wallpaper. Everything on
screen is drawn by the kernel itself: there is no libc, no bootloader code of
its own (it is a Multiboot kernel) and no external graphics or font libraries.

## What's inside

- **Kernel:** Multiboot entry, GDT/IDT, remapped 8259 PICs, a 1000 Hz PIT,
  a first-fit heap, the CMOS clock, PCI enumeration, ACPI power-off and reboot.
- **Drivers:** PS/2 keyboard and wheel mouse, Bochs/QEMU VBE (BGA) linear
  framebuffer at 1280x800x32, or a GRUB-provided framebuffer, the PC
  speaker for beeps, and an Intel PRO/1000 (e1000) network card.
- **Networking:** Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS and a TCP client
  with retransmission, written from scratch. On top of that, **HTTPS**:
  TLS 1.2 through [BearSSL](https://bearssl.org/), with certificates checked
  against the Mozilla root store built into the kernel. The HTTP/1.1 client
  follows redirects and understands chunked and gzip-compressed responses.
- **Graphics:** a dirty-rectangle compositor, antialiased rounded shapes,
  soft drop shadows, box blur for translucent "vibrancy" surfaces (the menu
  bar, dock, menus and Spotlight blur whatever is behind them), pre-rendered
  antialiased fonts (Inter, standing in for San Francisco), vector icons
  rasterised with 4x4 supersampling, a baseline **JPEG decoder** and bicubic
  scaling for the wallpaper.
- **Sign-in screen:** the wallpaper, frosted, with a big clock and date,
  account pictures, a password field (it shakes on a wrong password and
  then shows the hint), and Sleep / Restart / Shut Down. Lock Screen and
  waking from sleep come back to it with your windows still open.
- **Desktop:** a translucent menu bar with the Kepler menu, the frontmost
  app's name and menus, status icons and the clock; a dock with running-app
  dots, bouncing launches, minimised-window thumbnails and the Trash; desktop
  icons down the right edge; Launchpad; Spotlight (apps, files, or any
  command); Big Sur windows with traffic lights, rounded corners and
  shadows; macOS-style alerts and shut down / restart / log out prompts.
- **Kepler:** a web browser with a dark, starry start page, after the
  [Kepler](https://github.com/lenodrips-create/kepler) browser. Type a
  search or an address; searches go to DuckDuckGo Lite. It renders HTML
  text, headings, lists, quotes, tables, preformatted text and links, and
  submits simple forms. It has back/forward history, bookmarks and View
  Source. It doesn't run scripts or load CSS or images, so it suits
  text-first sites: Wikipedia, Hacker News, text.npr.org, lite.cnn.com.
  Spotlight can also search the web with it.
- **Apps:** Finder (sidebar, toolbar, icon view), Notepad, Calculator,
  Terminal (dir, cd, type, tree, echo with `>` redirection, calc, neofetch,
  tasklist, plus `ipconfig`, `nslookup` and `curl` for the network...), Paint, Preview, Minesweeper, Activity Monitor with live CPU
  and memory graphs, System Preferences (desktop background), System
  Information and About This Kepler.
- **Files:** an in-memory file system (Kepler HD) with a few starter
  documents. Files you create live until you power off.

| | |
| --- | --- |
| ![Sign-in screen](docs/screenshots/login.png) | ![Launchpad](docs/screenshots/launchpad.png) |
| ![Spotlight](docs/screenshots/spotlight.png) | ![Shut down](docs/screenshots/shut-down.png) |
| ![Kepler start page](docs/screenshots/kepler.png) | ![A page in Kepler](docs/screenshots/kepler-page.png) |

The page on the right was served over HTTPS by a local test server.

## Build and run

You need `gcc` that can target i386 (`-m32`), GNU `ld`, `make` and
`qemu-system-i386`.

```sh
make            # builds build/kepler.elf
make run        # boots it in QEMU
make iso        # optional: a GRUB CD image (needs grub-mkrescue + xorriso)
```

Pick **Kepler** on the sign-in screen (or press Enter) and type the password
**`kepler`**. **Guest** signs in without one.

### Getting online

`make run` gives QEMU an e1000 card on user-mode networking
(`-nic user,model=e1000`), so Kepler can reach the internet through your
computer with no setup. In VirtualBox, keep the default adapter (Intel
PRO/1000 MT Desktop) attached to NAT. In VMware, use the "e1000" adapter
type. KeplerOS gets an address by DHCP at boot; `ipconfig` in Terminal
shows it.

Certificates are checked against the clock, so the virtual machine needs
the right date. The trusted roots are in `net/ca_roots.c`.
`tools/gen_ca_roots.sh` regenerates that file from a directory of PEM
certificates with BearSSL's `brssl` tool.

The Windows key plays the Command key: tapping it opens **Launchpad**,
**Win+Space** (or Win+R) opens **Spotlight**, **Win+Q/W** closes the window,
**Win+M** minimises it, **Win+H** hides the app, **Win+L** locks the screen,
**Win+D** shows the desktop and **Win+E** opens a Finder window.
**Ctrl+Alt+Del** opens Activity Monitor, **Alt+Tab** cycles windows and
**Alt+F4** closes the active window. Menu shortcuts written `Ctrl+S` are
shown as Command-S.

## Layout

| Path | What |
| --- | --- |
| `boot/` | Multiboot header, entry point, interrupt stubs |
| `kernel/` | CPU setup, heap, timer, console, clock, PCI, power, `kmain` |
| `drivers/` | keyboard, mouse, framebuffer and the e1000 network card |
| `net/` | the network stack, DNS, TCP, entropy, the HTTP(S) client, gzip, and the trusted root certificates |
| `third_party/bearssl/` | BearSSL 0.6, built into the kernel for TLS (`third_party/compat/` has the few libc headers it needs) |
| `gfx/` | drawing, fonts, icons, cursors, JPEG decoder |
| `ui/` | shell main loop and compositor, window manager, desktop, menu bar, dock, Launchpad and Spotlight, menus, widgets, alerts, boot and sign-in screens |
| `apps/` | the applications |
| `fs/` | the RAM file system |
| `assets/` | the wallpaper and the Kepler icon, linked into the kernel image |
| `tools/genfont.py` | regenerates `gfx/fontdata.c` from TrueType fonts (needs Pillow) |
| `tools/qemu_drive.py` | boots QEMU headless and drives it over QMP to take screenshots |
| `tools/gen_ca_roots.sh` | regenerates `net/ca_roots.c` from PEM root certificates |

## Credits

- Fonts are rasterised from DejaVu Sans / Sans Mono (Bitstream Vera
  license), Liberation Sans (SIL OFL 1.1) and Inter (SIL OFL 1.1).
- The wallpaper and the Kepler icon are images supplied by the project owner.
- TLS is [BearSSL](https://bearssl.org/) by Thomas Pornin, MIT license (see
  `third_party/bearssl/LICENSE.txt`).
- The trusted root certificates come from Mozilla's CA store (MPL 2.0).
- Searches go to [DuckDuckGo Lite](https://lite.duckduckgo.com/lite/).
- The look is inspired by macOS Big Sur. KeplerOS is not affiliated with
  Apple and uses no Apple artwork or fonts; apart from the Kepler icon, its
  icons and logo are drawn in code.
