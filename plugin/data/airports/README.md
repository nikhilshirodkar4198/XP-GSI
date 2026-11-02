# Airport tables

`<ICAO>.json` files: airline and per-airline aircraft-type weights at each
airport, derived from reconstructed ground stays in ADS-B history.

**Source:** adsb.lol globe_history (https://github.com/adsblol), built by
`pipeline/build_tables.py`. Each file's `source` field names the sampled day(s).

**License:** these tables are a derived database made available under the
Open Database License (ODbL) v1.0, https://opendatacommons.org/licenses/odbl/1.0/.
Any rights in individual contents of the database are licensed under the
Database Contents License, https://opendatacommons.org/licenses/dbcl/1.0/.
If you publicly use, adapt or redistribute them, keep this notice and offer
the adapted database under the ODbL.

Airport positions used while building the tables came from OurAirports
(https://ourairports.com/data/, public domain). No gate coordinates are
stored here: the plugin takes stand positions only from the user's scenery.
