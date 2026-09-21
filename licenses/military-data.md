# Military reference data

Wikipedia contributors, **List of countries by number of military and paramilitary personnel**:
https://en.wikipedia.org/wiki/List_of_countries_by_number_of_military_and_paramilitary_personnel

Wikipedia contributors, **List of countries by level of military equipment**:
https://en.wikipedia.org/wiki/List_of_countries_by_level_of_military_equipment

Accessed 2026-09-18. Article histories provide contributor attribution. The Wikipedia-derived
reserve and ship counts in `data/source/military_2026.json` are adapted under
[Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
Changes: country-code mapping, ship category aggregation/exclusions and explicitly labelled
scenario estimates. That licence applies to the adapted Wikipedia data, not the application code.
Plan 21 replaced many reserve figures with model estimates; those entries are not Wikipedia-derived
and each says so in its `source`.

Aircraft observations: FlightGlobal / Cirium, **World Air Forces 2025**, directory pages 12–34.
https://www.flightglobal.com/download?ac=106507
Accessible publisher-document mirror:
https://www.airforce.mil.zm/pluginAppObj/pluginAppObj_710/world_air_forces_directory_20251_243110.pdf
Only factual country totals from the active combat-aircraft columns are recorded. The copyrighted
PDF, images and prose are not redistributed and are not described as Creative Commons content.

Spending observations: World Bank Open Data, indicator MS.MIL.XPND.CD,
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Spending keeps its World Bank
observation wherever there is one.

Active-duty strengths are **not** taken from a third-party dataset. MS.MIL.TOTL.P1 counts active
plus paramilitary personnel, so plan 21 replaced it for every actor with rounded model estimates,
each flagged `estimated: 1` and labelled as an estimate in its `source`. They carry no upstream
licence and must not be cited as World Bank or Wikipedia figures. The downloaded MS.MIL.TOTL.P1
values and their observation years are still retained in `country_stats.json` under CC BY 4.0 as
the record of what the World Bank publishes.
