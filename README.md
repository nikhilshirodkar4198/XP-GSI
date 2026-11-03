# XP-GSI — parked airliners at the gates for X-Plane

XP-GSI fills the gates and stands of the airport you are at with parked
aircraft in realistic airline liveries. It uses the CSL aircraft models you
already have (for example the ones xPilot installs) and puts them on the
stands of the scenery you have loaded.

**Key features**

- Real airlines at real airports: which airlines and aircraft types park
  where comes from months of recorded ADS-B traffic at about 900 airports,
  and from Wikipedia airline lists everywhere else.
- Airlines at their own terminals and piers, cargo airlines on cargo aprons.
- Works with any scenery: stand positions are read from the scenery
  installed in your X-Plane, never from a built-in list.
- Traffic-aware: a stand empties when you, a VATSIM pilot or an AI aircraft
  parks on it or pulls in.
- Reserve your arrival gate, from the XP-GSI window or by typing it into the
  Zibo / LevelUp 737 CDU.
- Light on the sim: work runs in the background and is spread over frames.

## Requirements

- X-Plane 11.50 or newer, or X-Plane 12.
- Windows 10/11 (64-bit), macOS 10.15 or newer (Intel or Apple Silicon), or
  64-bit Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Mint 21 or later).
- A CSL model library. The easiest way is to install
  [xPilot](https://xpilot-project.org), which downloads one. CSL libraries
  under `Resources/plugins`, `Custom Data` or `Custom Scenery` are found
  too.

**Recommended settings**

- In X-Plane's settings, turn **off "Draw parked aircraft"**, so X-Plane's
  own parked aircraft don't share the stands.
- Don't run XP-GSI together with Traffic Global, World Traffic or
  LiveTraffic's parked aircraft: they fill the same stands.

## Install

1. Download `XP-GSI-v0.9.0-beta.zip` from the [Releases page](https://github.com/nikhilshirodkar4198/XP-GSI/releases) and unzip it.
2. Copy the `XP-GSI` folder into `X-Plane/Resources/plugins/`.
3. Start X-Plane.

**macOS:** the plugin is not notarized yet. If X-Plane says it cannot load
XP-GSI, open Terminal and run (with your X-Plane path):

```
xattr -dr com.apple.quarantine "/path/to/X-Plane 12/Resources/plugins/XP-GSI"
```

## First run

- The first time, XP-GSI builds an index of your airports and scans your
  CSL models. This runs in the background and can take up to a minute; the
  window shows "building the airport index". Later starts take a second or
  two. The index is rebuilt by itself when you add or remove scenery.
- Open the window from **Plugins > XP-GSI**, or bind the command
  `xpgsi/toggle_window` to a key.
- Pick a **start mode**:
  - **Auto** — starts for every flight once it has loaded.
  - **Manual** — runs only when you press Start.
  - **VATSIM** — starts when xPilot connects you to VATSIM as a pilot and
    stops when you disconnect.

## Using it

**Density.** Two sliders set how full the airport is: *Passenger* and
*Cargo*. Both start on an automatic value from the airport's real daily
traffic; a change is remembered for that airport only. Untick *Cargo
aircraft on cargo aprons* if you don't want freighters or your scenery has
its own.

**VATSIM.** With xPilot connected, a stand empties as soon as another pilot
parks on it or pulls in. XP-GSI never touches xPilot's or X-Plane's
multiplayer aircraft: its statics are scenery objects only.

**Reserve gate.** Press *Reserve gate* in the window (or bind
`xpgsi/gate_window`) and pick your arrival stand: XP-GSI keeps it and its
neighbours free for you. In the Zibo / LevelUp 737 you can also type
`G` plus the stand name into the CDU scratchpad on the ground, for example
`GB12`.

**Commands to bind** (Settings > Keyboard or Joystick, search "XP-GSI"):

| Command | What it does |
|---|---|
| `xpgsi/toggle_window` | Open or close the XP-GSI window |
| `xpgsi/gate_window` | Open or close the reserve-gate window |

**Advanced** (in the window):

- *Max aircraft* — an optional cap on statics per airport, for slower PCs.
- *Restore cooldown* — off by default: a stand that traffic left stays
  empty. Set a time to have statics come back.
- *Draw distance* — statics farther away are not drawn.
- *Fixed seed* — keep the same layout every time.
- *Debug logging* — more detail in the log, for bug reports.
- The version and data date are shown at the bottom.

## How accurate is it?

- **About 900 airports with recorded traffic** (most large and medium
  airports in Europe, North America, Asia and Oceania): airlines, aircraft
  types and their terminals follow what really parks there. Long-haul
  international airlines appear only at the terminals they mainly use (at
  O'Hare, Terminal 5). Expect a good match, but not a copy of any particular
  day.
- **Airports with only a Wikipedia airline list:** the right airlines in
  roughly the right proportions, with types from each airline's fleet. Where
  terminals are known they are used; otherwise airlines can appear at any
  terminal.
- **Small airports without either:** few or no airliners, by design.
- How good it looks depends on your **CSL library**: an airline or type it
  lacks is replaced by the closest match, or the stand is left empty.
- Stand names, sizes and positions come from your **scenery**: a scenery with
  few or badly sized stands gives fewer or smaller aircraft. Only where a
  scenery has under half the stands its traffic needs are OpenStreetMap
  parking positions added; those have no ground markings.

## Troubleshooting

- **XP-GSI isn't in the Plugins menu.** Check the folder is
  `Resources/plugins/XP-GSI/` with `win_x64`, `mac_x64` or `lin_x64` inside
  (not a folder nested twice). On macOS see the quarantine step above.
  X-Plane's `Log.txt` says why a plugin failed to load.
- **No aircraft appear.** Is the mode *Manual* (press Start)? Does the window
  say no CSL models were found (install xPilot or a CSL library)? Is the
  density at 0 %? Some small airports have no airline traffic at all.
- **Grey or missing liveries.** That CSL model's texture is missing in your
  library; reinstall or update the library.
- **Performance.** Lower *Max aircraft* or *Draw distance*. Each static is a
  full CSL model, so a big hub with hundreds of statics costs some frame rate
  on slower GPUs.
- **The log** is `Resources/plugins/XP-GSI/logs/XP-GSI.log` (the previous
  session is `XP-GSI.prev.log`).
- **Reporting a bug:** turn on *Debug logging*, reproduce the problem, and
  attach `XP-GSI.log` and X-Plane's `Log.txt`, with the airport, the scenery
  you use and what you expected.

## License, credits and data

XP-GSI's code is released under the [MIT License](LICENSE). The data files
derived from the sources below keep those sources' licenses.


XP-GSI is built with [Dear ImGui](https://github.com/ocornut/imgui) and
[JSON for Modern C++](https://github.com/nlohmann/json) (both MIT); its CSL
reading follows [XPMP2](https://github.com/TwinFan/XPMP2) (MIT).

Its data comes from:

- [adsb.lol](https://adsb.lol) ADS-B history — ODbL 1.0
- [OpenStreetMap](https://www.openstreetmap.org/copyright) terminals and
  gates, © OpenStreetMap contributors — ODbL 1.0
- [Wikipedia](https://en.wikipedia.org) airport and airline articles,
  by Wikipedia contributors — CC BY-SA 4.0
- [Wikidata](https://www.wikidata.org) — CC0
- [OurAirports](https://ourairports.com/data/) — public domain

Full notices are in the `LICENSES` folder of the download.

XP-GSI is an independent X-Plane project. It is not affiliated with or
endorsed by GateStaticInjector for MSFS, Laminar Research / X-Plane, VATSIM,
xPilot, or any CSL library or its authors.
