# Changelog

## v0.9.0-beta — first public beta

First public release for X-Plane 11.50+ and X-Plane 12. Code under the MIT
License; derived data under ODbL 1.0 / CC BY-SA 4.0 (see LICENSES).

- Parked airliners on the gates and stands of the airport you are at, in
  airline liveries from your CSL library, on the stands of your installed
  scenery.
- Airline and aircraft-type mixes from recorded ADS-B traffic at about 900
  airports; Wikipedia airline lists elsewhere. Airlines kept at their own
  terminals, long-haul international airlines at the terminals they mainly
  use; cargo airlines on cargo aprons.
- Far too sparse sceneries (under half the stands their traffic needs) are
  filled in with OpenStreetMap parking positions.
- Start modes: Auto, Manual, VATSIM (follows xPilot's connection).
- Stands empty for you, VATSIM pilots and AI traffic parking or pulling in;
  optional restore cooldown.
- Arrival gate reservation from the window or the Zibo / LevelUp 737 CDU.
- Passenger and cargo density per airport, max aircraft, draw distance,
  fixed layout seed, debug logging.
- Log with version, data, X-Plane and OS details; capped at 5 MB with the
  previous session kept.
