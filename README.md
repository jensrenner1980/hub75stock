# hub75stock

Stock information on a wall of HUB75 LED matrix panels, driven by
[rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) on a
Raspberry Pi Zero 2 W.

Up to 3 electrical chains with up to 5 panels each (64x32 pixels per panel),
so up to 15 panels. Every panel is drawn independently: nothing is ever
rendered across a panel boundary.

## Hardware wiring

Current setup: one Waveshare 64x32 panel, direct-wired to the Pi's 40-pin
GPIO header - no HAT/adapter board yet (see *Adafruit HAT/Bonnet* below for
the planned upgrade). `matrix.hardwareMapping: "regular"` in the config
(the library's own default) is the correct value for this wiring - it turned
out to be an exact pin-for-pin match for Waveshare's own recommended
pinout for this panel, needing no adjustment.

### Power

The panel needs its own 5V supply, separate from the Pi's - **do not** power
a HUB75 panel from the Pi's own 5V rail or a USB port; these panels draw far
more current than that can supply. Double-check polarity before connecting:
what's printed on the panel's power connector is correct, but some
third-party supply cables have been reported with red/black reversed.

Sizing the supply: the library's own guidance is to budget for **~3.5A per
32x32 quadrant at full white** (`wiring.md` in the vendored library) as a
safe, conservative figure across panel brands - for a 64x32 panel like this
one, that's ~7A worst case. This project's own panel measured well under
that in practice: full-white draw settles around **700mA-1A** at 5V on a
lab bench PSU (30V/3A max, set to 5V, current limit at 3A - never hit),
confirmed for real via oscilloscope (probing OE and a data line, triggered
on LAT) that the configured brightness genuinely reaches the physical GPIO
signals rather than just trusting the software setting. That lower number
is this specific panel's own characteristic, not something to design a
supply around for a *different* panel - size for the conservative figure,
not this one's measured draw, especially once more panels are added to the
chain.

The Pi and the panel's logic share a common ground through the HUB75
ribbon's own GND pins (wired as part of the table below) - no separate
ground wire needed beyond what that connector already carries, as long as
every GND pin the table calls for is actually connected and not skipped as
"redundant".

### Data/control pins

The Raspberry Pi only drives 3.3V logic; many HUB75 panels are speced for
5V logic on their inputs. In practice, most panels read 3.3V as a valid
high just fine over a short cable, which is what this project's own single,
directly-wired panel does today. If you see glitches, erratic pixels, or
plan to run a longer cable or drive more panels, consider adding line
buffering/level-shifting - the vendored library ships open-hardware adapter
board designs for exactly this under
[`rpi-rgb-led-matrix/adapter/`](rpi-rgb-led-matrix/adapter/), which sit
between the Pi and the panel(s) and buffer/shift every signal properly
rather than relying on the panel's own tolerance for an out-of-spec input
level.

Essential connections for **one panel on one chain** (a subset of the full
`"regular"` mapping - see the vendored library's own
[`wiring.md`](rpi-rgb-led-matrix/wiring.md) for the complete 3-chain table if
wiring more than one chain directly rather than via a HAT):

| Pi pin | Signal      | GPIO (BCM) |
|-------:|-------------|:----------:|
| 6      | GND         | -          |
| 7      | Strobe/LAT  | GPIO4      |
| 11     | Clock       | GPIO17     |
| 12     | OE-         | GPIO18     |
| 13     | Chain 1 / G1 | GPIO27    |
| 15     | A           | GPIO22     |
| 16     | B           | GPIO23     |
| 18     | C           | GPIO24     |
| 19     | Chain 1 / B2 | GPIO10    |
| 21     | Chain 1 / G2 | GPIO9     |
| 22     | D           | GPIO25     |
| 23     | Chain 1 / R1 | GPIO11    |
| 24     | Chain 1 / R2 | GPIO8     |
| 26     | Chain 1 / B1 | GPIO7     |

`D` (pin 22) is needed for this panel's 1:16 multiplexing (32-row panels);
`E` (pin 10) is only needed for 64-row/1:32 panels and isn't wired here.
Any of the header's other GND pins work equally well as pin 6 - the table
above uses the one the upstream wiring diagram happens to reference.

`ledRgbSequence` may need adjusting from the default `"RGB"` depending on
the specific panel - this project's own Waveshare panel (FM6124DJ driver
ICs) needed `"RBG"`, discovered via the `color-cycle` test pattern showing
red correctly but green/blue swapped (see *Configuration* below). Worth
running `color-cycle` on any newly-wired panel before trusting its colours.

After wiring, verify with the hardware bring-up patterns before trusting
real stock data on it - `identify` (numbering/orientation), `panel-order`
(daisy-chain sequence), `border` (edges/alignment), and `color-cycle` (the
RGB mapping above) catch the most common wiring mistakes fastest; see
*Running* below for the full pattern list and how to invoke them.

### Adafruit HAT/Bonnet (planned)

An Adafruit "Triple LED Matrix Bonnet for Raspberry Pi, HUB75" (PID/MPN
6358, supports up to 3 parallel chains) is on order to replace the direct
wiring above once it arrives. Per Adafruit's own published schematic for
this board: its buffer chip wires
GPIO18->OE, GPIO17->CLK, GPIO4->Strobe, GPIO22->A, GPIO23->B, GPIO24->C -
an exact match for the `"regular"` mapping already in use, not the
`adafruit-hat`/`adafruit-hat-pwm` mapping the older, single-chain classic
Adafruit RGB Matrix HAT/Bonnet needs (which only supports one chain at all,
per the library's own mapping table - a three-chain board couldn't use it
regardless). OE already sitting on GPIO18 (a hardware-PWM-capable pin) from
the factory also suggests the classic GPIO4<->18 solder-bridge mod - which
exists specifically because *that* older HAT defaults OE onto GPIO4 instead
- won't be needed here either.

If that read holds up once the board is physically in hand: plug it in,
leave `hardwareMapping: "regular"` unchanged, no solder mod required. Worth
confirming with a continuity check against the table above before fully
trusting it, though, since this is inferred from a schematic image rather
than verified on real hardware.

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
    sudo apt install crossbuild-essential-arm64 qt6-base-dev:arm64 zlib1g-dev:arm64

This pulls in `gcc-aarch64-linux-gnu`/`g++-aarch64-linux-gnu`/
`binutils-aarch64-linux-gnu`/`libc6-dev-arm64-cross`/
`libstdc++-14-dev-arm64-cross` for the compiler, the arm64 build of Qt6 for
the target headers/libs, and `zlib1g-dev:arm64` for `pngwriter.cpp`'s
`libz.so`/`zlib.h` (the runtime `.so.1` is normally already on the Pi, but
cross-*linking* needs the unversioned dev symlink, which only the `-dev`
package installs) - all from Debian's own repos, nothing external.
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
* `--export-dir <path>` - where the web form's "Export current view as
  PNG" button saves to (see *Export current view as PNG*). Default
  `/tmp/hub75stock-exports`.
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
    "updateIntervalSeconds": 180,
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

`mode` is one of four equal, independent per-display choices - `list` (a
compact 4-line list), or `line` / `area` / `candles` (one stock at a time,
full panel, rotating every `rotationSeconds`). There is no separate global
"chart style" setting: each panel picks its own mode, so one wall can freely
mix a list panel with line/area/candlestick panels. Up to
`limits::kMaxSymbolsPerDisplay` (10) symbols per panel: `line`/`area`/
`candles` mode rotates through all of them one at a time regardless of
count; `list` mode shows 4 at once and, once there are more than 4 assigned,
scrolls through the rest on the same `rotationSeconds` cadence - a one-row
sliding window (row 0 shows the oldest visible symbol, row 3 the newest;
the symbol that scrolls off the top reappears at the bottom exactly one
tick after it left, not a new pass through the whole list) rather than
smooth pixel scrolling, which fits this project's LED-matrix aesthetic (and
its existing rotation mechanic) better than continuous motion would. With
4 or fewer symbols assigned, list mode is exactly as static as it always
was - the scrolling only ever kicks in once it's actually needed.

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
place on the change value and no thousands separator, as suggested. This is
what the `list16` bring-up pattern itself demonstrates, and the font
geometry above still holds exactly.

The live list mode (`renderList()` in `stockrenderer.cpp`, as opposed to the
`list16` bring-up pattern) has since moved to three independently-aligned
fixed-column blocks instead of one sequential left-to-right string, so the
same three pieces of content line up column-for-column across every row
regardless of how long a ticker name is or how many digits a price needs -
the whole point being visual uniformity down the panel, not just fitting
the width budget. Columns below are 0-indexed, matching the code:

* **Ticker**, left-aligned at column 0, plain fixed-pitch `DrawText()` (no
  compression - see below for why that matters). 4 characters get a 16px
  cell; a name that's exactly 5 characters after truncation (`.left(5)`,
  same cap chart mode's header already uses) gets a 20px cell instead of
  chopping a real 5-letter ticker down to 4. Shorter names are padded, not
  centred, so the *start* of every ticker always lines up at column 0.
* **Currency + price**, right-aligned so its own last (always-blank) column
  lands on column 46 - regardless of ticker length or how many digits the
  price needs, which is what guarantees at least one blank column between
  the two blocks even in the worst case (a 5-character ticker's own cell
  ends at column 20; verified for real, pixel-by-pixel, against a
  hand-drawn reference mockup before relying on it - see below). The price
  itself (`formatPriceFixedWidth()`) uses a fixed digit-character budget -
  5 total at 100 or above, 4 total under it - split between the integer and
  fractional parts by magnitude rather than a fixed decimal count:
  `1234.5` (4+1), `39.33` (2+2, the 4-digit budget), `5.500` (1+3, also the
  4-digit budget), `99999` (5+0, also the display cap - not a realistic
  equity price, but the fixed-width layout needs a hard ceiling
  regardless). The narrower 4-digit budget isn't just a formatting choice -
  since this whole block is right-aligned to column 46 regardless of its
  actual width, a shorter digit budget makes the block itself narrower,
  which shifts the currency symbol before it 4px further right
  automatically (verified for real: IONQ at $39.33 vs. AAPL at $333 landed
  exactly 4 columns apart) - no separate positioning logic needed for that,
  it's just what right-alignment already does with less content to fit.
* **Change**, right-aligned to column 64 (the panel's own right edge, since
  it's always the last content on the line). `formatChangePercentFixedWidth()`
  uses a sign plus exactly 2 digit characters total - one decimal place
  under 10% (`+6.7`), none at or above it (`+67`) - capped at ±99%.

Both number formatters, and the currency symbol before the price, still run
through `drawCompressed()` - the same glyph-by-glyph helper that compresses
`.` and ` ` from the font's normal 4px advance down to 2px (a `.`'s own ink
sits in column 1 of its cell, not column 0, so it's drawn one column
*before* its normal slot - flush against whatever precedes it while still
leaving the usual 1px gap before whatever follows, rather than the dot
ending up flush against the *next* character instead), plus a third rule
for `%` specifically: it advances only 3px, its own true ink width with no
reserved trailing blank column, since it's always the very last glyph on
its line with nothing after it that would need the usual gap. The ticker
deliberately does *not* go through this - its padding is meant to fill a
real 16px/20px cell at the font's normal pitch, and while drawing it
compressed wouldn't actually cause a visible collision (padding is blank
either way, and nothing depends on exactly where it ends), it would
silently stop matching the stated cell width for no benefit.

Each block's actual on-screen width varies with its content, so
right-aligning it means computing that width *before* drawing - a second
helper, `compressedWidth()`, mirrors `drawCompressed()`'s own per-glyph
advance table without drawing anything, purely so the two can never drift
out of sync with each other.

Price and change always share one colour (see *Live data* below) - a
deliberate simplification over the price ever having its own separate
after-hours dimming, so the two numbers on each row always read as one
consistent unit rather than two independently-styled elements.

**Readability at this size**: the font uses real disambiguation tricks (`0`
wider and flat-capped top and bottom vs plain rounded `O` - originally a
literal slash through the middle instead, retired in favour of this
because it reads as more consistent with the other digits and holds up
better against `8` at this size; a flagged `1` vs barred `I` vs plain `L`;
`Q`'s tail sitting outside the O/D silhouette) that hold up even at 3 px of
actual ink. The one weak spot is **`S` vs `5`**, a single-pixel difference
that may not survive LED diffusion - low practical risk here since digit
fields and ticker fields never mix, but worth a look on real hardware
before relying on it.

**Scrolling through more than 4 symbols**: a display can have up to
`limits::kMaxSymbolsPerDisplay` (10) symbols assigned, but only 4 rows
physically fit. With more than 4, `renderList()` shows a one-row sliding
window that advances by exactly one symbol every `rotationSeconds` - row *r*
shows `symbols[(offset + r) % totalSymbols]`, with `offset` ticking forward
on the same `elapsedMs`/`rotationSeconds` cadence chart mode already rotates
symbols on. Deliberately a discrete once-per-tick row swap, not smooth pixel
scrolling - fits the rest of this project's LED-matrix aesthetic (and its
one existing rotation mechanic) better than continuous motion would, and
needed no new timing concept, just reusing the one chart mode already had.
Verified for real against the actual formula: exporting a 6-symbol list at
four points in time and decoding the rendered glyphs pixel-by-pixel showed
rows `A B C D` -> `B C D E` -> `C D E F` -> `E F A B` - each tick shifting
the window by one, and the wrap-around (`F` looping back to `A`/`B` rather
than running off the end) landing exactly where the formula predicts. With
4 or fewer symbols assigned, `offset` stays 0 always - list mode is exactly
as static as it always was; the scrolling only engages once it's actually
needed.

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

### Export current view as PNG

A button on the web form saves a PNG snapshot of *every configured panel's
currently displayed view* - whatever's actually on screen at that exact
moment, chart or list mode, real hardware or `--dry-run` - to
`--export-dir` (default `/tmp/hub75stock-exports`; created if it doesn't
exist).

One 20x20px block per real LED: a 2px black border framing a 16x16 fill,
either that LED's actual lit colour or `#0f0f0f` for one that's genuinely
unlit (pure black) - so a 64x32 panel becomes a 1280x640 image. A blue
"hub75stock" watermark (36px tall, the embedded 4x6 font scaled up 6x -
chosen specifically so 6 * 6px native height lands on exactly 36, not an
approximation) sits in the bottom-**left** corner - deliberately not
bottom-right, which is where the connectivity indicator (see below) always
lives on row 1/column 1's panel; the two would otherwise compete for the
same spot on that panel's export.

Filenames are `R<row>C<column>_<yyyyMMdd_HHmmss>.png` - not the symbol(s)
currently shown, since list mode can show up to four simultaneously and
chart modes rotate through their own over time, so panel position is the
only label that's ever unambiguous.

This needed one real piece of plumbing: nothing in the rendering pipeline
could previously be *read back* once drawn - `PanelView` only exposed
`SetPixel` (write), and on real hardware the canvas is a write-only,
one-way GPIO output by design. `PanelView` now mirrors every
`SetPixel`/`Fill` call into a small in-memory shadow buffer
(`shadowPixel()`), cleared in step with `MatrixWall::clear()` each frame -
`panelexport.cpp` reads that back rather than needing hardware support that
doesn't exist.

The watermark is rendered with the project's own embedded BDF font (via the
library's own `DrawText()`, onto a throwaway `MemoryCanvas`, then blitted
into the PNG scaled up) rather than Qt's `QPainter`/`QFont` text APIs -
confirmed directly that those crash without a `QGuiApplication` (even with
`QT_QPA_PLATFORM=offscreen` set), and this project only ever constructs a
`QCoreApplication`.

More generally, `panelexport.cpp` touches no Qt6::Gui at all - the pixel
buffer is a plain `QByteArray`, saved via `pngwriter.cpp`'s own minimal PNG
encoder (an `IHDR`/`IDAT`/`IEND` writer over zlib's `compress2()`/`crc32()`)
rather than `QImage::save()`. That's deliberate, not a style preference:
`libqt6gui6`'s own *hard* dependencies (not just Recommends) pull in the
entire X11/EGL/OpenGL/input library stack - confirmed for real to be more
than a Raspberry Pi's SD card can spare. `pngwriter.cpp` needs only zlib,
already a near-universal, tiny dependency (see "Cross-compiling" above for
the one extra package this adds to that setup: `zlib1g-dev:arm64`, on the
build host only - nothing extra to install on the Pi itself).

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
| red (255,64,64)   | no WiFi link at all                      |
| blue (0,140,255)  | WiFi connected, but no internet reachable |
| green (0,255,96)  | online                                   |

Purely a network-layer signal, deliberately - data-fetch health used to be
folded into this same indicator as a fourth/fifth colour, but that meant
the one global indicator stayed stuck on the worst symbol's state even
once *other* symbols had already recovered - confirmed confusing for real
on live hardware (a config with a mix of US and Xetra symbols, watched
across a real Xetra session open: some tickers had fresh data again while
the corner was still amber, because it was reporting the worst case across
every tracked symbol, not just the ones actually recovering). Data health
now lives directly on each affected symbol's own price/change instead (see
below) - more precise, since it names *which* symbol has a problem, and it
can't lag behind individual recoveries the way one aggregate did. Reuses
the same green/red the stock colouring already uses (an earlier version of
this project used a dedicated palette specifically to avoid that overlap,
then deliberately switched to this instead - green/red mean "good"/"bad"
consistently everywhere on the wall, this corner included).

Backed by `nmcli` (device state for the WiFi link, `nmcli -t -g
CONNECTIVITY general` for actual internet reachability, which is
NetworkManager's own periodic check and already handles captive portals
etc.) rather than a second, separate reachability probe - consistent with
the rest of the project's WiFi handling (see `provisioning/`), and the
calls run fully asynchronously so a slow or hung `nmcli` never stalls the
render loop.

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

`updateIntervalSeconds` is clamped to 60..900 (default 180) - deliberately
never faster than once a minute: Yahoo's own bars are 1-minute granularity
and the free feed already lags ~15-20 min behind real time, so polling
faster than that buys no freshness and only adds load. The default moved
from a flat 60s to 180s after a real, extended fetch stall was observed on
live hardware after several hours of once-a-minute polling for multiple
symbols - not confirmed as rate-limiting specifically, but conservative
polling is cheap insurance against it either way (see the `qWarning()`
fetch-failure logging and the connectivity indicator's red "data issue"
state in `YahooDataProvider`, both added for visibility into exactly this).

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

Beyond the OHLC bars, the response's `meta` object carries a few more
authoritative, server-computed fields this project uses directly rather
than deriving its own equivalents: `regularMarketPrice` (the currently
*displayed* price - a genuinely live tick, not just this session's last
*completed* bucket's close, which can lag it by up to a whole bucket's
width - 8.5 min for Xetra) and `regularMarketDayHigh`/`regularMarketDayLow`
(used only to *widen*, never shrink, the chart's auto-scaled vertical
range - our own bucket data can undershoot the true day extremes early in a
session, or during the reporting lag above, if the tick that set the real
high/low hasn't landed in a completed bucket yet). Both fall back to the
bucket-derived equivalent if Yahoo ever omits them. A small, expected
side-effect: the header's displayed price and the chart's own rightmost
point aren't guaranteed to match to the last cent at every instant - the
header shows the most current number available, the chart shows the
completed history, and that's a feature of using the more current source
where it's actually more current, not a bug.

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

`marketClosed: "grey"` only actually shows grey once the *displayed session
itself* is stale - a previous day's frozen close (e.g. Friday's chart still
showing Monday morning before the next open), not simply "market's closed
for today." This needed its own signal (`StockSnapshot::sessionAnchorEpoch`,
the displayed session's own first bar) precisely because the response
metadata can't tell "after hours, same day" and "before today's open, still
showing yesterday" apart on its own: before today's own session has
started, `currentTradingPeriod.regular` already describes *today's
upcoming* session even while the bars actually returned are still
yesterday's, so both situations look identical as "now is before that
metadata's session start." Comparing the session's own anchor timestamp
against the viewer's local calendar date does distinguish them.

After today's own close but still the same calendar day ("after hours"),
nothing dims at all - the ticker, the last price, the day's % change, and
the chart itself all stay at full, undimmed colour. That's deliberate, not
an oversight: none of them are made any less true by the market being
closed - the chart is a complete record of the whole day regardless of
when you're looking at it, the day's % change is still exactly correct
once the session is over, and (an earlier version of this project had the
last price dim on its own here, on the reasoning that it's the one number
that stops moving once the session ends - since retired in favour of
always matching the change colour, so the two read as one consistent unit
on every row instead of two independently-styled elements; see *Compact
list mode* above) the last price is simply whatever it closed at, same as
the rest. `marketClosed: "grey"` still applies once the session itself is
stale (see above), and `"normal"`/`"blank"` are unaffected by any of this
either way - `"normal"` never overrides colour regardless of staleness,
`"blank"` blanks the panel the whole time the market isn't open regardless
of staleness.

A symbol whose most recent fetch attempt had a problem
(`StockDataProvider::DataHealth`, queried per-symbol from the provider, not
a wall-wide aggregate - see *Connectivity status indicator* above for why)
gets its price and change tinted amber instead - `applyDataHealth()` in
`stockrenderer.cpp`, applied on top of whatever colour the market-state
logic above already picked, in both list and chart mode. Deliberately
amber, not red: red already means "price down" right next to these same
numbers, and a stale figure shown in red would read as a price move that
never actually happened. Two shades distinguish two genuinely different
situations, both confirmed for real: bright amber for `DataHealth::NoData`
(the fetch succeeded, Yahoo just hasn't published any bars yet - seen for
real for the first ~20-30 minutes after a Xetra session's own 09:00 CEST
open), dim amber for `DataHealth::Error` (an actual failure - a network
error, a timeout, or Yahoo itself returning an API-level error object,
logged via `qWarning()` in `YahooDataProvider::handleReply()` and visible
in `journalctl -u hub75stock` under the systemd service). Reuses the same
"dim = less certain" language gap-bridged chart lines already use elsewhere
in this file, so the more serious of the two situations reads as the *more*
uncertain one rather than the brighter, more attention-grabbing one.
Crucially, this never discards the price itself the way falling back to a
dashed placeholder would - a fetch problem is usually transient (self-heals
on the next successful poll, same as everywhere else in this provider), and
the last-known price is still useful context during that window, just
flagged as possibly stale. Dashes are reserved for a symbol that's *never*
successfully fetched at all (a freshly-added or genuinely broken config
entry), a different, more absolute situation that amber tinting a
previously-good number doesn't apply to.

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
real segments already are (green/red/white live or after-hours, grey only
for a stale session styled that way) rather than a separate hardwired colour -
that reads as "less certain", needs no special-casing against the
closed-market grey styling, and doesn't reuse a hue already spoken for by
the [connectivity indicator](#connectivity-status-indicator).

Line/area mode colours **per point, relative to the session's reference
price** - green above it, red below - rather than one verdict for the whole
chart based only on where the session currently stands. That's the same
convention real intraday stock apps use (Yahoo Finance, Robinhood, Google
Finance): it's strictly more informative than a single colour, since a
stock that dipped below its reference mid-session and recovered actually
shows the dip instead of rendering as one solid colour for the entire
chart. A connecting line whose two endpoints land on opposite sides of the
reference is split exactly where it crosses it - green above the line, red
below, precisely at the crossing pixel rather than the whole segment
switching to whichever colour the *destination* point happens to be (the
simpler alternative most point-to-point charts use, tried first here too,
but a visibly worse look: a segment heading down into red territory would
render red even for the portion still geometrically above the line). Area
mode's fill stops at the reference line rather than running all the way to
the chart's bottom edge - a below-reference point fills red only between
the reference line and that point, not the panel's full height - matching
the same real-world convention rather than one solid colour block
regardless of which side of the reference the price sits on. Candlestick
mode is deliberately **not** part of this: each candle keeps colouring
itself by its own open vs. close within that time slice (the standard,
different candlestick convention), not by position relative to the session
reference.

Fetches for multiple symbols run concurrently (capped at a handful at a
time) rather than one giant burst - a deliberate, if informal, courtesy
towards an API with no documented rate limit to respect in the first place.

## Structure

    CMakeLists.txt                     build: native and cross, one file
    cmake/toolchain-aarch64-rpi.cmake   arm64 cross-compilation toolchain
    src/config/appconfig.*    JSON config: parsing, validation, defaults
    src/config/symbol.*       EXCHANGE:TICKER[:ALIAS] parsing and provider mapping
    src/display/matrixwall.*  owns the hardware, hands out one view per panel
    src/display/panelview.*   a Canvas over one panel's 64x32 sub-rectangle,
                              with a readback shadow buffer for panelexport.*
    src/display/panelexport.* "export current view as PNG" web form button
    src/display/pngwriter.*   minimal PNG encoder (zlib only, no Qt6::Gui)
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

## Preparing a fresh Raspberry Pi OS Lite image

An end-to-end walkthrough for turning a stock Raspberry Pi OS Lite (64-bit,
Debian 13/trixie) install into a working `hub75stock` deployment. Assumes
the cross-compiled binary already exists (see *Cross-compiling* above) and
the hardware is wired per *Hardware wiring* above.

### 1. Flash and get a shell

Raspberry Pi Imager, 64-bit "Raspberry Pi OS Lite" - the Imager's own
advanced options (hostname, SSH enabled, locale) can be set at flash time
for a one-off deployment. For the "flash once, provision many devices from
the same generic image" workflow this project is actually built around,
skip that and use the boot-partition mechanisms instead: `wificonfig.json`
for network credentials (see *WiFi provisioning* above) and
`hub75stock.json` on the boot partition for the app's own config (see
*Configuration* above) - both droppable onto an already-flashed card from
any OS via a card reader, no SSH needed before first boot at all. Either
way, first access is over SSH once the Pi has an IP (`ssh
<user>@<hostname>.local` if mDNS resolves, otherwise find the IP via the
router or - once the app itself is running with `--web-config-port` - the
[startup QR splash](#startup-qr-splash) on the panel itself).

### 2. Update and trim the base install

    sudo apt update && sudo apt full-upgrade

Per the vendored library's own troubleshooting guidance
(`rpi-rgb-led-matrix/README.md`), a minimal image is a more reliable one for
driving these panels - fewer background processes competing for timing-
sensitive CPU time. Raspberry Pi OS Lite already starts fairly minimal;
removing what's still not needed for a headless display device is optional
but recommended:

    sudo apt-get remove bluez bluez-firmware pi-bluetooth triggerhappy pigpio

### 3. Disable onboard audio

The library's hardware-pulse timing shares a subsystem with onboard sound -
confirmed necessary for this project, not just a hypothetical: the library
actively refuses to start (`--led-no-hardware-pulse` aside) if it detects
the `snd_bcm2835` module loaded. Both steps below are needed; some
distributions load the module even with audio nominally off at the
`dtparam` level:

    echo "dtparam=audio=off" | sudo tee -a /boot/firmware/config.txt
    echo "blacklist snd_bcm2835" | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
    sudo update-initramfs -u

Reboot and confirm with `lsmod | grep snd_bcm2835` (no output expected)
before wiring up the panel.

### 4. Install runtime dependencies

    sudo apt install libqt6httpserver6 libqt6websockets6 libqrencode4

`libqt6core6t64`/`libqt6network6t64` come in transitively as dependencies of
the packages above - no need to list them separately. Deliberately **not**
`libqt6gui6`: its own hard dependencies pull in the entire X11/EGL/OpenGL/
input library stack, confirmed for real to be more than a Raspberry Pi
Zero 2 W's SD card can spare, and nothing in this project actually needs it
(see *Export current view as PNG* above for why). `zlib1g` (used by
`pngwriter.cpp`) and NetworkManager (`nmcli` - used by WiFi
provisioning and the [connectivity indicator](#connectivity-status-indicator))
both ship as part of a stock Raspberry Pi OS Lite image already; nothing
extra to install for either.

### 5. Optional: reserve a CPU core for display refresh

The app itself suggests this at startup if it's missing (see
`rpi-rgb-led-matrix/lib/gpio.cc`) on any multi-core Pi. Add to the end of
the existing line in `/boot/firmware/cmdline.txt` (same line, no newline):

    isolcpus=3

Reserves the last core purely for the panel refresh thread - worth it if
anything else meaningful runs on the same Pi, per the vendored library's
own CPU-use notes.

### 6. Boot-partition write access for the unprivileged runtime user

Needed specifically because `provisioning/hub75stock.service` points
`--config` at `/boot/firmware/hub75stock.json` (the boot partition, chosen
so the config is droppable from any OS - see *Configuration* above): the
app starts as root (needed for `/dev/mem`), but `rpi-rgb-led-matrix` drops
privileges to the unprivileged `daemon` user immediately after GPIO setup,
*before* the web config form ever handles a save request (see *Hardware
wiring*/*Web config form* above). Saving a change through the form then
means writing to `/boot/firmware/hub75stock.json` as `daemon`, not root.

FAT32 - unlike NetworkManager's own storage, or any real Linux filesystem -
has no actual per-file Unix permissions of its own; what `ls -l` shows for
files on it is entirely synthesised by the kernel's FAT driver from the
mount's own `uid=`/`gid=`/`umask=` options, uniformly for every file on the
partition. By default those usually resolve to root-only-writable, which is
exactly why an unprivileged save can otherwise fail silently or with a
permission error. Confirm `daemon`'s actual UID/GID first (standard Debian
value is `1`/`1`, but don't assume - check):

    id daemon
    blkid | grep /boot/firmware    # for the PARTUUID already in /etc/fstab

Then edit the existing `/boot/firmware` line in `/etc/fstab` to add the
matching `uid=`/`gid=`, e.g.:

    PARTUUID=xxxxxxxx-01  /boot/firmware  vfat  defaults,uid=1,gid=1,umask=022  0  2

`umask=022` keeps it group/other-readable but only owner (`daemon`)
-writable - tighter than leaving it wide open, since anything else on the
system can still read `hub75stock.json` (no secrets in it) but not silently
rewrite it. `mount -o remount /boot/firmware` (or reboot) to apply without
needing to unmount by hand.

### 7. Deploy the binary and config, install the services

    scp build-hub75stock-pi/hub75stock config/hub75stock.json <user>@<pi>:/home/hub75stock/

(`provisioning/hub75stock.service`'s shipped `ExecStart` assumes this exact
path - edit it first if deploying somewhere else, see *Running at boot*
above.) Then, on the Pi:

    sudo cp provisioning/hub75stock-wifi-setup.sh /usr/local/bin/
    sudo cp provisioning/hub75stock-wifi-setup.service /etc/systemd/system/
    sudo cp provisioning/hub75stock.service /etc/systemd/system/
    sudo systemctl enable hub75stock-wifi-setup.service
    sudo systemctl enable --now hub75stock.service

(Skip the `wifi-setup` unit entirely if WiFi was already configured another
way, e.g. via the Imager's own advanced options at flash time - see *WiFi
provisioning* above for what it's for.)

### 8. Verify

    sudo systemctl status hub75stock
    journalctl -u hub75stock -n 50 --no-pager

The panel should show the [startup QR splash](#startup-qr-splash) briefly
(if `--web-config-port` is set and `wlan0` already had an IP at that exact
moment), then real stock content. Any fetch problems from here on show up
both in the journal (`YahooDataProvider`'s own `qWarning()` diagnostics) and
as the [connectivity indicator](#connectivity-status-indicator) turning
red.

## License

GPLv2-only (see [`LICENSE`](LICENSE)) - not a stylistic choice: `rpi-rgb-led-matrix`'s
sources are compiled directly into the `hub75stock` binary (see
*Cross-compiling* above), not linked as a separate shared library, and every
one of its source files is licensed under GPL "version 2" with no "or any
later version" clause (`GPL-2.0-only`, incompatible with GPLv3 for combining
code). That makes the combined binary a single work under GPLv2's terms,
so this project's own code is licensed the same way. Every source file
under `src/` (not the vendored `rpi-rgb-led-matrix/`, which keeps its own
original headers) carries an `SPDX-License-Identifier: GPL-2.0-only` line.
