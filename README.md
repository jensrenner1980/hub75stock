# hub75stock

Stock information on a wall of HUB75 LED matrix panels, driven by
[rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) on a
Raspberry Pi Zero 2 W.

Up to 3 electrical chains with up to 5 panels each (64x32 pixels per panel),
so up to 15 panels. Every panel is drawn independently: nothing is ever
rendered across a panel boundary.

## Status

Build system, configuration, panel addressing, hardware test patterns, stock
rendering (list mode, and all three chart variants), and now **live data**
are done - real intraday OHLC history and current prices from Yahoo Finance
by default (see *Live data*), with the synthetic `MockDataProvider` still
available via `--mock` for development without a network.

Verified on real hardware (one physical 64x32 panel, direct-wired per
Waveshare's/the library's `"regular"` pinout): `identify`, `panel-order`, and
hardware PWM brightness scaling down to the individual GPIO signal, confirmed
with an oscilloscope. Verified on an x86_64 host via `--dry-run`: config
parsing/validation, panel addressing for unequal chains, all ten test
patterns, the text layout budget, list mode, all three chart variants (area
fill shape, per-candle colouring, line-only), rotation timing, the render loop
and clean SIGINT shutdown. The project also cross-compiles cleanly to a
genuine aarch64 binary - confirmed with `file`/`objdump`, not just "it
configured". Live data fetching itself was verified against real Yahoo
Finance responses on the development host (`--dry-run`, real network) -
correct OHLC aggregation, multiple concurrent symbols across several
exchanges (including one that surfaced and fixed a real bucketing bug, see
*Live data*), and correct market-open/closed detection - but not yet
exercised on-Pi for an extended run.

## Building

The build is CMake-based (there is no `.pro`/qmake project - qmake cannot
practically cross-compile against Debian's multiarch Qt layout, see below).

### Host (x86_64), for development

Needs the Qt 6 development files:

    sudo apt install qt6-base-dev

Then:

    cmake -G Ninja -B ../build-hub75stock -DCMAKE_BUILD_TYPE=Release
    cmake --build ../build-hub75stock

The vendored library sources are compiled directly into the executable, so the
result is a single self-contained binary with no `librgbmatrix.so` and no font
files to deploy. `lib/librgbmatrix.a` is never used.

The library needs a real Raspberry Pi and root at runtime, so on the host use
`--dry-run` (see below).

### Raspberry Pi Zero 2 W (aarch64), cross-compiled

qmake needs a Qt that was **built for aarch64**; the host's own `qmake6`
cannot target the Pi. Debian solves this without building Qt from source: its
Qt6 packages declare `Multi-Arch: same`, so the arm64 dev files (headers, .so,
CMake config) install *alongside* the amd64 ones via multiarch, no separate
sysroot tree needed.

One-time setup (needs root; run in a real terminal, not `sudo -S`/piped input
- it needs an interactive prompt):

    sudo dpkg --add-architecture arm64
    sudo apt update
    sudo apt install crossbuild-essential-arm64 qt6-base-dev:arm64

This pulls in `gcc-aarch64-linux-gnu`/`g++-aarch64-linux-gnu`/
`binutils-aarch64-linux-gnu`/`libc6-dev-arm64-cross`/
`libstdc++-14-dev-arm64-cross` for the compiler, and the arm64 build of Qt6
for the target headers/libs - all from Debian's own repos, nothing external.
Check what Qt version your Pi actually has installed
(`dpkg -l | grep libqt6core6t64` over SSH) - if it doesn't match what
`apt-cache policy qt6-base-dev:arm64` offers on the host, there's a real risk
of a runtime symbol-version mismatch (Qt's ABI compatibility promise is at the
`major.minor` level, e.g. any 6.8.x is fine against any other 6.8.x, but 6.8
against 6.10 is not guaranteed).

Then:

    cmake -G Ninja -B ../build-hub75stock-pi \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-rpi.cmake \
          -DCMAKE_BUILD_TYPE=Release
    cmake --build ../build-hub75stock-pi

`cmake/toolchain-aarch64-rpi.cmake` points `CMAKE_PREFIX_PATH` straight at
`/usr/lib/aarch64-linux-gnu/cmake` (the arm64 Qt6 config dir) rather than at a
sysroot, and sets `QT_HOST_PATH=/usr` so Qt's build system uses the
host-native `moc`/`uic`/`rcc` (Debian keeps those at `/usr/lib/qt6/libexec/`)
instead of trying to run the target's own non-executable-here arm64 tools.
Verified end to end: the resulting binary is a genuine
`ELF ... ARM aarch64 ... dynamically linked, interpreter /lib/ld-linux-aarch64.so.1`
executable (confirmed with `file`), refuses to run on the host
("Exec format error", as expected), and a `Q_OBJECT` class in a throwaway test
project linked clean, proving `moc` really ran via the host tools rather than
silently no-opping.

Copy the resulting `hub75stock` binary to the Pi (e.g. `scp`) and run it there
as described below.

The project deliberately does **not** use the library's own `config.mk`, which
hardcodes `-march=native -mtune=native` and only suppresses it for compilers
literally named `*aarch64-linux-gnu*` - this build compiles the library's
sources directly as part of the CMake target instead, under one consistent
set of flags for both the host and the cross build. Consider adding
`-mcpu=cortex-a53` (the Zero 2 W's actual core) to
`cmake/toolchain-aarch64-rpi.cmake` once there's real hardware to benchmark
against.

## Running

The panels need `/dev/mem`, so on the Pi run as root. **Without `--pattern`,
it runs the real stock display**, backed by live Yahoo Finance data unless
`--mock` is given (see *Live data*):

    sudo ./hub75stock --config config/hub75stock.json

With `--pattern <name>`, it renders a hardware bring-up pattern instead
(`--list-patterns`):

| Pattern       | What it verifies                                            |
|---------------|-------------------------------------------------------------|
| `identify`    | panel numbering, arrangement, orientation (top-left marker) |
| `panel-order` | the daisy-chain sequence, one panel at a time               |
| `border`      | panel edges and alignment                                   |
| `color-cycle` | the RGB channel mapping                                     |
| `greyscale`   | PWM bits and gamma                                          |
| `text-grid`   | the 4 lines x 10 characters text budget                     |
| `chase`       | that no drawing bleeds between panels                       |
| `list16`      | the 4x6, 4-line x 16-char list-mode layout                  |
| `chart-header-stacked` | ticker + stacked price/change header, chart = 2/3 panel |
| `chart-header-inline`  | ticker + inline price/change header, chart = 3/4 panel  |

Useful flags:

* `--dry-run` - no hardware; validates the config and previews a frame as
  ANSI colour blocks on the terminal. This is how to work on the host.
  Adding `--duration <s>` runs the real render loop against an in-memory
  canvas, so the animated patterns and the signal handling can be exercised
  off-Pi; the last frame is printed on exit.
* `--show-config` - print the resolved configuration and exit.
* `--duration <s>` - stop after n seconds.
* `--font <5x8|4x6|tom-thumb>` - the embedded font to use.
* `--mock` - synthetic data instead of fetching real prices from Yahoo
  Finance (see *Live data*). Ignored with `--pattern`.
* any `--led-*` flag of rpi-rgb-led-matrix overrides the JSON file; use this
  for the panel quirks that have no JSON key yet, e.g.
  `--led-panel-type=FM6126A`, `--led-multiplexing=1`, `--led-row-addr-type=1`.
  `--led-help` lists them. Both `--led-brightness=25` and
  `--led-brightness 25` work, and `--show-config` reports the resolved
  values, flags included.

  The four geometry flags `--led-rows`, `--led-cols`, `--led-chain` and
  `--led-parallel` are **rejected** rather than applied: panel addressing is
  derived from `matrix.chains`, so a flag that disagreed with it would leave
  the code drawing at the wrong pixels. Change `matrix.chains` instead.

On a non-Pi host the library prints `Could not determine Pi model` to stderr
before any of our own output, including for `--show-config`. It is harmless -
the model probe runs while the `--led-*` flags are parsed - and silent on a Pi.

`Ctrl+C` (SIGINT) and SIGTERM shut down cleanly and blank the panels. This
matters: the refresh thread keeps rows lit, and being killed mid-frame can
leave a row at full current.

## Configuration

See `config/hub75stock.json`. Panels are addressed 1-based as
**row = chain**, **column = position within that chain**.

Without `--config`, the file is looked for in this order, first match wins:
`/boot/firmware/hub75stock.json` (highest priority - the boot partition, so a
config can be dropped onto the SD card from any OS via a card reader, no
SSH/root shell on the Pi needed, same rationale as `provisioning/`'s
`wificonfig.json`), then `config/hub75stock.json` (relative to the working
directory), then next to the binary itself, then `/etc/hub75stock/hub75stock.json`.

```json
{
  "matrix": {
    "chains": [3, 2, 1],
    "hardwareMapping": "regular",
    "ledRgbSequence": "RGB",
    "brightness": 60,
    "gpioSlowdown": 1
  },
  "global": {
    "updateIntervalSeconds": 60,
    "rotationSeconds": 15,
    "marketClosed": "grey"
  },
  "displays": [
    { "row": 1, "column": 1, "mode": "area", "symbols": ["IONQ"] },
    { "row": 1, "column": 3, "mode": "list",
      "symbols": ["AAPL", "MSFT", "GOOGL", "AMZN"] }
  ]
}
```

`matrix.chains` declares only *how many* panels hang on each chain; where they
physically sit on the wall is a human concern. Chains may differ in length -
the library still needs a rectangular canvas, so it is sized to the longest
chain and the unused tail of the shorter chains stays dark.

`mode` is one of four equal, independent per-display choices - `list` (all
stocks at once, no chart), or `line` / `area` / `candles` (one stock at a
time, full panel, rotating every `rotationSeconds`). There is no separate
global "chart style" setting: each panel picks its own mode, so one wall can
freely mix a list panel with line/area/candlestick panels. Up to 4 symbols
per panel (in `line`/`area`/`candles` mode, rotated through one at a time; in
`list` mode, all shown at once).

Symbols use a provider-independent `EXCHANGE:TICKER` notation (`ASX:BRN`,
`XETR:SAP`) or a bare ticker for US listings (`IONQ`). The exchange is
translated per data provider; the table lives in `src/config/symbol.cpp` and
unknown exchanges are rejected at config load time rather than silently
guessed.

An optional third segment, `EXCHANGE:TICKER:ALIAS`, sets a friendlier
on-panel label than the ticker. Some exchanges list a foreign stock under
their own internal code rather than its home-market ticker - e.g. IonQ
(Nasdaq: `IONQ`) trades on Xetra as `0YB0`, so `XETR:0YB0:IONQ` fetches the
right instrument but still displays "IONQ" rather than "0YB0". Finding that
code in the first place: searching Yahoo Finance's own site/API for the
home-market ticker (`IONQ`) does *not* surface foreign listings - search by
the exchange-native code instead (visible in some other tools, e.g. Google
Finance's `0YB0:FRA` notation), or try it directly at
`https://finance.yahoo.com/quote/<code>.<suffix>` for the suffix of the
exchange you're after (`.DE` Xetra, `.F` Frankfurt, `.SG` Stuttgart, `.MU`
Munich, `.HM` Hamburg, `.DU` Düsseldorf, ...). Xetra vs Frankfurt for a
European cross-listing is otherwise just a liquidity preference - the chart
sizes its buckets to whatever that exchange's session length actually is
(see *Live data*), so neither one runs into a fixed-width limitation the
other doesn't.

`ledRgbSequence` corrects for panels whose HUB75 connector mislabels its own
colour channels internally - e.g. a panel whose "G" pins actually drive its
blue LEDs and vice versa, which is a real, confirmed property of at least one
panel used during development (Waveshare, FM6124DJ driver ICs): `color-cycle`
showed red correctly but green and blue swapped, fixed with `"RBG"`. Must be a
3-letter permutation of `R`, `G`, `B`; validated at config load time rather
than left to the library's own `abort()` on a malformed value. If your panel's
channels are wired normally, leave it at the default `"RGB"`.

## Layout budget

### Full-panel text (`identify`, `text-grid`)

The embedded `5x8` BDF font is 5 px wide, 8 px tall, baseline 7. On a 64x32
panel that gives exactly **4 lines of 10 characters**: the four baselines at
y = 7, 15, 23, 31 make the glyph rows 0-7, 8-15, 16-23 and 24-31, filling the
panel with no clipping (descenders on the bottom line land on row 31), and ten
characters at 5 + 1 px kerning end at column 57, leaving 6 px spare.
`text-grid` renders exactly this, and `--dry-run` was used to confirm the
numbers above.

### Compact list mode (`list16`)

The embedded `4x6` font is 4 px wide, 6 px tall, baseline 5, and every glyph's
DWIDTH is 4 with ink only in columns 0-2 - the 4th column is always blank, so
**zero kerning already gives 1 px of spacing** between characters. That makes
**16 characters at 4 px = 64 px** an exact fit, no kerning needed.

For the requested 1 px blank row above and below each character line: fitting
each 6 px glyph line into an 8 px pitch (line pitch = 4 x 8 = 32 px, the same
pitch as the 5x8 grid above) leaves exactly 2 spare rows per line, split
symmetrically. The baseline for line *i* (0-indexed) is
`i*8 + font.baseline() + 1` - the "+1" over the 5x8 formula is that spare row.
Verified on real hardware output (`--dry-run --pattern list16`), including the
edge case of a descender (the tail of "Q") landing on the last glyph row and
not spilling into the padding row.

16 characters is enough for `TICKER PRICE CHANGE` with no truncation for
normal cases, e.g. `NVDA 178.42 -1.9` (4 + 1 + 6 + 1 + 4 = 16) - one decimal
place on the change value and no thousands separator, as suggested.

**Readability at this size**: the font uses real disambiguation tricks (a
slashed `0` vs plain `O`, a flagged `1` vs barred `I` vs plain `L`, `Q`'s tail
sitting outside the O/D silhouette) that hold up even at 3 px of actual ink.
The one weak spot is **`S` vs `5`**, a single-pixel difference that may not
survive LED diffusion - low practical risk here since digit fields and ticker
fields never mix, but worth a look on real hardware before relying on it.

### Single-stock chart header (`chart-header-stacked`, `chart-header-inline`)

Two header layouts were tried for "ticker top-left, price/change top-right,
chart below":

* **`chart-header-stacked`** (recommended): ticker in `5x8` top-left; price
  and change stacked directly on top of each other in `4x6`, right-aligned,
  no gap between them. 12 px header (rows 0-11), 20 px chart (rows 12-31) =
  **62.5%**, the closest exact fit to "lower two thirds". Collision-free even
  for a 5-character ticker like `GOOGL` next to a 6-digit price - 12 px of
  clear space between them, measured on the real render.
* **`chart-header-inline`**: ticker in `5x8` top-left; price and change on one
  combined `4x6` line top-right. 8 px header, 24 px chart = 75% - more chart
  area, but a 5-character ticker (`GOOGL`) **overlaps the price by 11 px** on
  the real render. Would need the ticker capped at 4 characters to be safe,
  which cuts out names like GOOGL, LCID, RIVN, PLTR.

`chart-header-stacked` is the one to build on: it matches the requested
proportions more closely and has no ticker-length collision risk.

Fonts are embedded via Qt resources and unpacked to a temp file at startup,
because `rgb_matrix::Font` can only load from a file path.

## WiFi provisioning

`provisioning/` lets one "generic" SD card image (no WiFi credentials baked
in via Raspberry Pi Imager) be reused for multiple deployments, without
re-imaging per deployment and without the app itself ever touching WiFi
credentials - that's deliberately left to NetworkManager's own
`.nmconnection` profile storage, rather than inventing a new place to keep a
passphrase.

Before first boot, drop a `wificonfig.json` onto the **boot partition**
(`/boot/firmware/wificonfig.json` - the same partition `config.txt`/
`cmdline.txt` live on, so no new workflow to learn):

```json
{
  "ssid": "MyNetwork",
  "passphrase": "correct horse battery staple"
}
```

(template at `provisioning/wificonfig.json.example`). Install the provisioning
script once per image:

```
sudo cp provisioning/hub75stock-wifi-setup.sh /usr/local/bin/
sudo cp provisioning/hub75stock-wifi-setup.service /etc/systemd/system/
sudo systemctl enable hub75stock-wifi-setup.service
```

On first boot with a `wificonfig.json` present, the service hands the
credentials to `nmcli device wifi connect` - which creates the usual
root-only-readable `/etc/NetworkManager/system-connections/*.nmconnection`
profile, exactly as if you'd typed the command yourself - then **blanks the
passphrase back out of `wificonfig.json`** (keeping `ssid`, for reference).
Safe on every subsequent boot: once the passphrase is blank, the script exits
immediately without touching networking again.

Worth being explicit about one tradeoff: while the passphrase is still
present, it sits in a FAT32 partition with no Unix file permissions at
all - unlike NetworkManager's own storage, which is `600`/root-only from the
moment the profile is created. The SD card's physical security is what
protects the passphrase during that brief window before first boot. That's
an inherent property of any boot-partition-based provisioning scheme (the
same tradeoff the classic `wpa_supplicant.conf`-on-boot-partition technique
has), not something specific to this script - and it only matters until the
first successful boot, after which the passphrase no longer exists in
plaintext anywhere on the card.

## Running at boot

`provisioning/hub75stock.service` starts the app itself automatically on
boot. `/etc/rc.local` is **not** a reliable way to do this on current
Raspberry Pi OS - it's a legacy SysV-init mechanism systemd only runs
through a compatibility shim (`rc-local.service`) that recent images often
don't ship at all, so it can silently never fire even with a correct,
executable, manually-runnable script. A real unit avoids that uncertainty
entirely, and gets proper `journalctl` logging and restart-on-crash for
free.

```
sudo cp provisioning/hub75stock.service /etc/systemd/system/
sudo systemctl enable --now hub75stock.service
```

Edit the unit's `ExecStart` first if the binary isn't at
`/home/hub75stock/hub75stock`, or if you want different flags (e.g. a
different `--web-config-port`, or `--mock`). Runs as root (needed for
`/dev/mem`, same as running it by hand with `sudo`) - `rpi-rgb-led-matrix`
drops that back down to an unprivileged user right after GPIO setup, same as
always (see *Web config form* below for why that means ports below 1024
don't work for `--web-config-port`).

`After=hub75stock-wifi-setup.service` orders it after the one-time WiFi
provisioning above, if that's installed - harmless if it isn't (a unit
that isn't present is simply skipped for ordering purposes), and only
really matters on the very first boot.

## Web config form

`--web-config-port <port>` starts an unauthenticated web form (all
interfaces) for editing the day-to-day settings without SSH/pulling the SD
card - brightness, auto-dim placeholder, timing, market-closed style, and
each panel's mode/symbols. Deliberately *not* `matrix.chains` or any other
GPIO/hardware-wiring setting, which stays hand-edit-only: it's a physical
wiring fact (how many panels are actually daisy-chained on each electrical
chain), riskier to fat-finger remotely, and rarely needs changing at runtime.

The form lists every position `matrix.chains` physically wires up, not just
the ones that already have a display configured - a position with nothing
configured yet shows as "(not configured)" with blank fields. Typing symbols
into it adds a display there; blanking an existing one's symbols removes it.
This is the only way the *set* of displays changes size - `matrix.chains`
itself, which decides how many positions exist in the first place, is only
ever read here, never written.

```
sudo ./hub75stock --config hub75stock.json --web-config-port 8080
```

Then visit `http://<the-pi>:8080/` from any device on the same network. No
authentication by design - reachable only from your own WiFi is treated as a
sufficient trust boundary for a personal desk device; add `--web-config-port`
only when you actually want the form running.

Pick a port >= 1024 (8080 is the natural choice). Ports below 1024 need
`CAP_NET_BIND_SERVICE`, and `rpi-rgb-led-matrix` deliberately drops root back
to an unprivileged user right after its GPIO setup - by the time the web
server starts, the process can no longer bind a privileged port even if you
launched it with `sudo`. That's intentional upstream behaviour (least
privilege for the long-running process), not a bug, and not worth working
around by disabling the library's privilege drop just to claim port 80.

Saving **validates through the exact same `AppConfig` parsing/validation
code path** the app itself uses (no second, drifting copy of the schema
rules) before writing anything - an invalid value is rejected with the
specific error and the file is left untouched, never partially written.
Changes take effect on the **next restart**, not live - the form just reads
and writes the config file directly and has no connection to a possibly
already-running `hub75stock` process.

Needs `qt6-httpserver-dev` and `qt6-websockets-dev` (the latter is a runtime
dependency of Qt's HTTP server module, not something this project uses
directly) at build time, for both the host and the `:arm64` cross target.
At deploy time, the Pi itself needs the matching **runtime** libraries:

```
sudo apt install libqt6httpserver6 libqt6websockets6
```

### Startup QR splash

The first panel (row 1, column 1) shows a scannable QR code for
`http://<wlan0-ip>:<port>/`, with the "hub75" / "stock" wordmark stacked
next to it, for 5 seconds - so a phone can go straight from "point camera at
panel" to the config form without anyone having to find the Pi's IP address
first. No repeated IP/port text next to the code - that's already spelled
out by the QR code itself, showing it twice would just be redundant.

Two things gate it, both necessary rather than one standing in for the
other: `--web-config-port` must actually be running (otherwise there is no
URL to encode), and `wlan0` must already have an IP address *at the exact
moment the app starts up* - checked once, synchronously, before the render
loop's first frame, not via the periodic
[connectivity monitor](#connectivity-status-indicator) that drives the
corner indicator. That one polls `nmcli` asynchronously, which meant a
version of this that waited on it would briefly show normal stock content
before the splash popped in once connectivity resolved - looked like a
glitch. Checking `wlan0` directly (a plain, instant OS interface read, no
subprocess involved) avoids that entirely: either there's already an IP at
startup and the splash plays immediately for a clean first 5 seconds, or
there isn't and it's simply skipped for this run - never something that can
appear or flicker in mid-runtime. The IP is looked up from `wlan0`
specifically (not "any non-loopback interface"), since a Pi with the USB
gadget/RNDIS port wired up (see *WiFi provisioning*) has another candidate
interface that isn't the one a phone on the same WiFi actually needs.

Built with [libqrencode](https://fukuchi.org/works/qrencode/) (vendoring a
hand-rolled QR encoder was never seriously considered - Reed-Solomon error
correction and module placement are exactly the kind of format-correctness
code where a subtle bug just silently produces something that fails to
scan). Needs `libqrencode-dev` at build time, for both the host and the
`:arm64` cross target; the Pi needs the runtime library:

```
sudo apt install libqrencode4
```

The QR code is drawn at the smallest version libqrencode picks for the URL,
checked against the panel's fixed 32px height (the binding constraint, since
a QR code is always square and every panel is 64x32) - comfortably fits
typical `http://192.168.1.50:8080/`-length URLs with room to spare. On the
vanishingly unlikely chance a URL doesn't fit (e.g. an unusually long
hostname), the splash is just skipped rather than drawing something cropped
or unscannable; the plain-text `Web config form: ...` line printed to
stdout at startup is unaffected either way.

## Connectivity status indicator

A 2x2px overlay in the bottom-right corner of the first panel (row 1, column
1 - always that one position regardless of wiring), refreshed every 60
seconds, showing WiFi/internet state at a glance without needing to pull up
`nmcli`:

| Colour            | Meaning                                  |
|-------------------|-------------------------------------------|
| magenta (255,0,128) | no WiFi link at all                      |
| amber (255,140,0)   | WiFi connected, but no internet reachable |
| blue (0,140,255)    | online                                    |

Deliberately *not* the red/green/white/grey already used for stock
colouring, so a glance at the corner never reads as a price move. Backed by
`nmcli` (device state for the WiFi link, `nmcli -t -g CONNECTIVITY general`
for actual internet reachability, which is NetworkManager's own periodic
check and already handles captive portals etc.) rather than a second,
separate reachability probe - consistent with the rest of the project's
WiFi handling (see `provisioning/`), and the calls run fully asynchronously
so a slow or hung `nmcli` never stalls the render loop.

Every display mode uses its panel's entire 64x32 area, so there is no corner
that's guaranteed free of stock content in every mode - the indicator is
drawn last, on top of whatever's already there, as a deliberate small
trade-off rather than an attempt at pixel-perfect non-interference. Toggle
it with `global.showConnectivityIndicator` (default `true`) - either by
hand-editing the JSON, or via the [web config form](#web-config-form)'s
"Show WiFi/internet status indicator" checkbox.

## Live data

By default, stock data comes from Yahoo Finance's chart endpoint - there is
no official public Yahoo Finance API; this is the same
unofficial/reverse-engineered one many hobby projects use
(`/v8/finance/chart/<symbol>?range=1d&interval=1m`, no API key). It has no
authentication, but also no guarantee of staying available in its current
form - Yahoo could change the response shape or start blocking unrecognised
clients without notice. `--mock` swaps in the synthetic `MockDataProvider`
instead (a random walk seeded per-symbol, no network involved) for
development/testing or just exercising the display without depending on
Yahoo being reachable.

`YahooDataProvider` and `MockDataProvider` both implement the same
`StockDataProvider` interface (`src/stocks/stockdataprovider.h`), so
`main.cpp` and `stockrenderer.cpp` don't need to know or care which is
active.

Each `updateIntervalSeconds` tick, every configured symbol's *entire*
current session is refetched (1-minute bars, aggregated client-side into
buckets) and the old snapshot is replaced wholesale, rather than
incrementally patching it. That's simpler than incremental patching, and it
means a lost connection - or one transient fetch failure - self-heals on the
very next successful poll: there's no accumulated local state that can drift
from reality. A symbol that has never successfully fetched (or whose
exchange isn't in the mapping table, see `Symbol::toYahooSymbol()`) just
shows the renderer's existing dashed "no data yet" placeholder rather than
anything crashing or hanging.

Bucket width is sized dynamically per exchange - the *regular session
length* (`sessionEnd - sessionStart`, which is stable day to day even
though the exact start/end timestamps aren't reliable, see below), divided
into `pricedata::kBucketCount` buckets - rather than a fixed number of
minutes. A fixed width would mean exchanges with shorter regular sessions
(NYSE/Nasdaq: 6.5h) fill visibly less of the chart than ones with longer
sessions (Xetra: ~8.5h; Frankfurt floor trading: ~14h), for no reason a
viewer would find meaningful - every session's chart should look like a
*complete* day, whatever that exchange's actual hours are, not a shorter one
just trailing off part-way across the panel. `MockDataProvider`'s synthetic
session doesn't have this problem (no real exchange hours to vary) and
keeps a fixed width, `pricedata::kBucketMinutes`.

Deliberately *not* sized from how much data happens to be available at
fetch time (i.e. the span between the first and last *returned* bar) -
tried that first, and it looked right for a completed session but broke
badly for one still in progress. Confirmed for real: at 09:51 CEST, 51
minutes into Xetra's 09:00 open, the newest bar Yahoo's free/unauthenticated
feed actually returned was timestamped 09:33 - a real ~15-20 minute
reporting lag on top of barely any bars existing yet this early anyway. That
made the *observed* span tiny, which when divided into 60 buckets produced
very fine buckets that the (relatively few) available minutes then nearly
filled on their own - a chart reading as "almost half full" after roughly a
tenth of the session had actually elapsed, not proportional to real
progress through the day at all. Sizing from the session's fixed, known
length instead keeps the bucket width constant all day, so the chart fills
up in proportion to *actual* elapsed session time and reaches exactly 100%
at close - "left edge is open, right edge is close", not "left edge is
open, right edge is however much data happened to exist a moment ago".

Bucket aggregation deliberately keys off the *data's own first timestamp*,
not `meta.currentTradingPeriod.regular.start` from the response - on a
request made outside that period (over a weekend, or for an exchange far
enough from UTC that "today" server-side isn't the same calendar day as the
exchange's), that metadata can describe a different session than the one
the timestamp array actually covers, which silently produced zero usable
buckets for every data point (hit for real testing an ASX symbol on a
Sunday). The market-open/closed state shown via `marketClosed` *does* still
come from that metadata (compared against the current time), which is a
separate, correctly-scoped use of it.

A thinly-traded listing can have long stretches with no trades at all
(confirmed for real testing `IONQ`'s Xetra cross-listing - most exchanges
list foreign stocks under their own internal code rather than the
home-market ticker, see *Configuration* below, and this particular one
barely trades). Each `PriceBucket` carries a `hasData` flag rather than
being silently omitted, so a slot's position in the array - and so its
column on screen - always matches its actual elapsed session time, never
"whichever real data point happened to come next"; two real trades an hour
apart don't end up looking adjacent just because nothing happened in
between. Candlestick mode simply draws nothing for a no-data slot. Line/area
mode draws a connecting line across the gap, dimmed rather than at full
brightness, so the overall day's shape stays legible without a gap chopping
it into disconnected specks - but visibly flagged as "no data here", not a
claim that the price moved smoothly through a stretch there's no
information about. Deliberately a dimmed version of whichever colour the
real segments already are (green/red/white when the market's open, grey
when closed and styled that way) rather than a separate hardwired colour -
that reads as "less certain", needs no special-casing against the
closed-market grey styling, and doesn't reuse a hue already spoken for by
the [connectivity indicator](#connectivity-status-indicator).

Fetches for multiple symbols run concurrently (capped at a handful at a
time) rather than one giant burst - a deliberate, if informal, courtesy
towards an API with no documented rate limit to respect in the first place.

## Structure

    CMakeLists.txt                     build: native and cross, one file
    cmake/toolchain-aarch64-rpi.cmake   arm64 cross-compilation toolchain
    src/config/appconfig.*    JSON config: parsing, validation, defaults
    src/config/symbol.*       EXCHANGE:TICKER[:ALIAS] parsing and provider mapping
    src/display/matrixwall.*  owns the hardware, hands out one view per panel
    src/display/panelview.*   a Canvas over one panel's 64x32 sub-rectangle
    src/display/memorycanvas.*  offscreen RGB buffer + ANSI preview (--dry-run)
    src/display/fontstore.*   embedded BDF fonts
    src/display/textutil.*    shared glyph-measurement/alignment helpers
    src/display/testpatterns.*  hardware bring-up patterns
    src/display/stockrenderer.* the real per-panel rendering: list mode,
                                line/area/candlestick charts
    src/stocks/pricedata.h    PriceBucket / StockSnapshot - the OHLC data model
    src/stocks/stockdataprovider.h  common interface both providers implement
    src/stocks/mockprovider.*       synthetic data, for dev/testing (--mock)
    src/stocks/yahoodataprovider.*  the real fetcher (default)
    src/net/connectivitymonitor.*  polls nmcli for WiFi/internet state
    src/web/configserver.*    unauthenticated web form for the config file
    provisioning/             one-time WiFi setup (script, systemd unit,
                              template) and the app's own systemd unit

`PanelView` is the piece that makes per-panel drawing work. The library exposes
the whole wall as one large canvas; a `PanelView` maps local (0,0) to a panel's
corner and clips writes to its own rectangle, so drawing code - including the
library's own `DrawText()`/`DrawLine()` - can be written as if it owned a
standalone 64x32 display.

## Next steps

1. Bring the rest of the physical wall online as more panels/the Adafruit HAT
   arrive, and update `matrix.chains` / `displays` in the config to match.
2. Live data (see *Live data* above) has only been exercised via `--dry-run`
   on the development host so far, not yet on-Pi for an extended run - worth
   watching for how it behaves over hours/days (rate limiting, symbols that
   go stale, etc.) once deployed.
