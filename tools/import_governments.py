#!/usr/bin/env python3
"""Write data/source/governments_2026.json from the per-system mapping below.

The curated JSON is the artifact the game reads; this exists because 239 expanded rows are not
reviewable but twelve lists of country codes are. Re-run it after editing the mapping, then
`python3 tools/import_stats.py` to load the result into data/world.sqlite. It touches no network
and no geometry. The twelve values must stay in step with validGovernment() in
src/status/regimes.cpp and GOVERNMENTS in tools/import_stats.py.
"""
import json, pathlib, sqlite3

ROOT = pathlib.Path(__file__).resolve().parents[1]

SYSTEMS = {
 'parliamentary_republic': """
   AL AM AT BA BB BD BG CH CZ DE DM EE ET FI FJ GE GR HR HU IE IL IN IQ IS IT KI LB
   LV MD ME MH MK MT MU NP NR PK PL RS SG SI SK SM SO TT VU WS XK ZA
 """,
 'presidential_republic': """
   AO AR BI BJ BO BR BW CD CF CI CL CO CR CY DO DZ EC EG FM GH GM GT GY HN ID KE
   KG KM KR LR MR MV MW MX NA NG PA PE PH PW PY SC SL SR SS SV TN TR US UY ZM
 """,
 'semi_presidential_republic': """
   CV FR GW LK LT MG MN MZ PT RO SN ST TL TW UA
 """,
 'constitutional_monarchy': """
   AD AG AU BE BH BS BT BZ CA DK ES GB GD JM JO JP KN KW LC LI LS LU MA MC MY NL NO NZ
   PG SB SE TH TO TV VC
 """,
 'absolute_monarchy': """ AE BN OM QA SA SZ """,
 'theocracy': """ AF IR VA """,
 'one_party_state': """ CN CU ER KP LA VN """,
 'dominant_party_state': """ AZ BY CG CM DJ GA GQ KH KZ NI RU RW TD TG TJ TM TZ UG UZ VE ZW """,
 'military_junta': """ BF GN ML MM NE SD """,
 'transitional_government': """ EH HT LY PS SY YE """,
 'dependent_territory': """
   AI AS AW AX BL BM CK CW FK FO GG GI GL GU HK IM IO JE KY MF MO MP MS NC NF NU PF
   PM PN PR SH SX TC VG VI WF
 """,
 'uninhabited_territory': """ AQ GS HM TF UM """,
}

# Constitutional/founding year, where it is a well-established fact. Anything not listed
# stays 0, which the game reads as "long-standing or unknown" and does not display.
SINCE = {
 'AF': 2021, 'AL': 1998, 'AM': 2018, 'AO': 2010, 'AT': 1945, 'AZ': 1995, 'BA': 1995,
 'BD': 1972, 'BE': 1831, 'BF': 2022, 'BG': 1991, 'BR': 1988, 'BY': 1994, 'CA': 1867,
 'CD': 2006, 'CH': 1848, 'CL': 1980, 'CN': 1949, 'CO': 1991, 'CU': 1959, 'CZ': 1993,
 'DE': 1949, 'DK': 1849, 'EE': 1991, 'EG': 2014, 'ES': 1978, 'ET': 1995, 'FI': 1919,
 'FR': 1958, 'GB': 1689, 'GE': 2024, 'GN': 2021, 'GR': 1975, 'GW': 1994, 'HR': 1991,
 'HT': 2024, 'HU': 2012, 'ID': 1945, 'IE': 1937, 'IL': 1948, 'IN': 1950, 'IQ': 2005,
 'IR': 1979, 'IT': 1948, 'JP': 1947, 'KP': 1948, 'KR': 1987, 'KZ': 1991, 'LA': 1975,
 'LT': 1992, 'LV': 1991, 'LY': 2011, 'MA': 2011, 'ME': 2006, 'MK': 1991, 'ML': 2021,
 'MM': 2021, 'MN': 1992, 'MX': 1917, 'MZ': 1990, 'NE': 2023, 'NG': 1999, 'NL': 1815,
 'NO': 1814, 'NP': 2015, 'NZ': 1907, 'PK': 1973, 'PL': 1997, 'PS': 1994, 'PT': 1976,
 'QA': 2003, 'RO': 1991, 'RS': 2006, 'RU': 1993, 'RW': 2003, 'SA': 1932, 'SD': 2021,
 'SE': 1974, 'SI': 1991, 'SK': 1993, 'SS': 2011, 'SY': 2024, 'TD': 2024, 'TH': 2017,
 'TJ': 1994, 'TL': 2002, 'TM': 1991, 'TN': 2022, 'TR': 2018, 'TW': 1996, 'TZ': 1977,
 'UA': 1996, 'UG': 1995, 'US': 1789, 'UY': 1967, 'UZ': 1991, 'VA': 1929, 'VE': 1999,
 'VN': 1976, 'XK': 2008, 'ZA': 1996, 'ZW': 2013,
}

# Rows where the classification departs from the Factbook's formal label, and why.
NOTES = {
 'RU': 'Formally semi-presidential; classified by the absence of competitive turnover.',
 'BY': 'Formally presidential; classified by the absence of competitive turnover.',
 'KH': 'Formally a parliamentary constitutional monarchy; the CPP holds every seat.',
 'TM': 'Formally presidential; the Democratic Party is the only permitted party of government.',
 'EH': 'Non-self-governing territory; administration disputed between Morocco and the SADR.',
 'PS': 'Palestinian Authority; authority divided between the West Bank and Gaza.',
 'YE': 'Internationally recognised government and rival authorities divided by civil war.',
 'LY': 'Rival administrations in Tripoli and the east.',
 'SY': 'Transitional authorities following the fall of the Assad government.',
 'HT': 'Transitional Presidential Council; no sitting legislature.',
 'VA': 'Absolute elective monarchy exercised by an ecclesiastical office.',
 'AE': 'Federation of seven hereditary emirates.',
 'VE': 'Formally a presidential republic; classified by the absence of competitive turnover.',
 'NI': 'Formally a presidential republic; classified by the absence of competitive turnover.',
}

SOURCE = "CIA World Factbook 'Government type' (2025) and the state's own constitution"


def parse(block):
    out = []
    for token in block.split():
        assert not token.endswith('?'), f'unresolved marker {token}'
        out.append(token)
    return out


def main():
    codes = [c for c, in sqlite3.connect(ROOT / 'data/world.sqlite')
             .execute('SELECT code FROM countries ORDER BY code')]
    assigned = {}
    for system, block in SYSTEMS.items():
        for code in parse(block):
            assert code not in assigned, f'{code} assigned twice: {assigned.get(code)} and {system}'
            assigned[code] = system
    missing = [c for c in codes if c not in assigned]
    extra = [c for c in assigned if c not in codes]
    assert not missing, f'unassigned: {missing}'
    assert not extra, f'not an actor: {extra}'

    countries = {}
    for code in codes:
        row = {'system': assigned[code], 'since': SINCE.get(code, 0), 'source': SOURCE}
        if code in NOTES:
            row['source'] = SOURCE + '. ' + NOTES[code]
        countries[code] = row
    document = {
        'note': ('Government type in force on 1 January 2026. Classifications follow each state\'s '
                 'constitution and the CIA World Factbook "Government type" field; rows that depart '
                 'from the Factbook\'s formal label say so in their own source note. "since" is the '
                 'year the current constitutional order began, 0 where it is long-standing or not '
                 'well established. This is a game starting point, not an authoritative '
                 'constitutional classification, and it makes no claim about sovereignty.'),
        'license': 'CIA World Factbook content is in the public domain; the classification itself is project-owned.',
        'countries': countries,
    }
    (ROOT / 'data/source/governments_2026.json').write_text(
        json.dumps(document, indent=1, sort_keys=True, ensure_ascii=False) + '\n')
    counts = {}
    for code in codes:
        counts[assigned[code]] = counts.get(assigned[code], 0) + 1
    for system in SYSTEMS:
        print(f'{counts.get(system, 0):4d}  {system}')
    print(f'{len(codes):4d}  total')


main()
