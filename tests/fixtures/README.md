# Recorded reference fixture

`portugal-page.json` was recorded on 2026-09-16 from:
https://en.wikipedia.org/w/rest.php/v1/page/Portugal/with_html

Revision: 1375265884. The actual response metadata is retained; HTML is shortened to two sections to keep the fixture small. Wikipedia contributors, CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/), source article https://en.wikipedia.org/wiki/Portugal. This truncation is an adaptation for testing. Country-code and entity responses in core_tests.cpp are synthetic fixtures, not a record of a separate Wikipedia page lookup.

`legacy-lifecycle-v{0,2,3,4}.json` are synthetic exports captured on 2026-09-23
before Plan 26 production edits, using the original `resolve` and monthly rules.
Version 0 omits monthly status metadata; 2 predates unrest and front momentum;
3 predates unrest; 4 includes it. Each spans three turns with construction
completion, temporary occupation treaty expiry, an active war, and displacement.
They contain invented actors, no private save content. Treat expected state AND
events as immutable compatibility evidence; never regenerate them to accept a
new rule. CTest runs each through `replay_verify`. The lifecycle suite separately
creates a mixed old/new export in the build directory and tests migration retries.
