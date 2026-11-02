XP-GSI @VERSION@ - static aircraft at the gates for X-Plane 11 and 12
=======================================================================

XP-GSI fills the gates and stands of the airport you are at with parked
aircraft in realistic airline liveries, using the CSL models you already
have installed (for example with xPilot).

INSTALL
  1. Copy this XP-GSI folder into  X-Plane/Resources/plugins/
  2. Make sure a CSL library is installed (xPilot installs one).
  3. In X-Plane's settings, turn OFF "Draw parked aircraft" so X-Plane's
     own parked aircraft don't share the stands.
  4. Start a flight. The XP-GSI window is under Plugins > XP-GSI.

The first start builds an airport index and scans your CSL models; this can
take up to a minute. Later starts are fast.

macOS: if the plugin does not load, remove the download quarantine:
  xattr -dr com.apple.quarantine "<X-Plane>/Resources/plugins/XP-GSI"

Do not run XP-GSI together with Traffic Global, World Traffic or
LiveTraffic's parked aircraft.

Problems? The log is XP-GSI/logs/XP-GSI.log (and X-Plane's Log.txt). Please
attach both to a bug report.

Full documentation: https://github.com/nikhilshirodkar4198/XP-GSI

XP-GSI is an independent X-Plane project. It is not affiliated with
GateStaticInjector for MSFS, Laminar Research / X-Plane, VATSIM, xPilot or
any CSL library. Third-party notices and data attribution: LICENSES/
