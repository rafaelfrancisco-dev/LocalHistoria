# Verification evidence

Verified on macOS / Apple silicon on 2026-09-16, using AppleClang 21, Drogon 1.9.13, JsonCpp 1.9.8, SQLite and CMake. This describes executed checks, not a production-readiness claim.

## Core and data

`make test` builds the app and replay verifier and passes both CTest suites. The C++ executable reports **47 checks passed**, covering deterministic resolution, serialization, all investment costs, starting-snapshot affordability, illegal/self targets, enum/shape validation, alliance acceptance/rejection and reciprocal offers, next-month priority effects, malformed model fallback, timeout fallback, constrained request shape, duplicate/idempotent submissions, overlap prevention, transaction rollback, narration isolation, save/reload, interrupted jobs, reference mappings and attribution, missing/ambiguous mappings, recorded Wikipedia response parsing, bounded introduction/history excerpts and offline proposals.

The Python data suite verifies 258 unique feature IDs, France/Norway/Kosovo/Taiwan normalization, unresolved metadata handling, 239 actor records, 152,970 city records, separate country/city Wikidata fields, absence of synthetic national population totals and complete supplied flag coverage.

## Real HTTP and browser

`python3 tests/integration.py` starts actual Drogon servers against disposable temporary databases. A controlled loopback model server drives real HTTP timeouts and malformed responses. Concurrent duplicate requests return one job; competing keys and stale/illegal orders fail without state changes. The suite checks alliance lifecycle, frozen reference revisions after cache replacement, save/restart equivalence, and process-kill recovery before commit.

The replay executable reconstructs three turns from persisted initial state and intentions, comparing every resulting state and authoritative event. It rejects a tampered replay. No inference is needed for replay.

Headless Chromium verifies country-list selection, a coordinate click on the map, zoom, structured orders, a text proposal that leaves the turn unchanged, editing that proposal, explicit submission, polling, journal events, page reload and resume. It also deliberately returns 404 for a flag to exercise the text fallback, selects Antarctica with no city records, and verifies persistence across server restart. External browser requests are blocked; none are attempted. No JavaScript runtime errors occur. The expected missing-flag 404s are fixture responses, not absent vendored files. The desktop screenshot was inspected. Screenshots remain in ignored `tests/artifacts/`.

## Wikipedia and model boundaries

A live call through the actual Drogon transport succeeded for Portugal: country code PT → Wikidata Q45 → English article Portugal, revision **1375265884**. The returned cache contains article URL, retrieval time, language, attribution, a bounded introduction and a separate bounded history excerpt. A shortened recorded API response is included with attribution under `tests/fixtures/`. Live source articles are not pinned to January 2026. Other countries and upstream outages are handled as unavailable; they were not exhaustively checked live.

No suitable local GGUF weights or llama-server executable were present. **Real Qwen inference, memory use, throughput and narrative quality remain unverified.** The tested local-model integration uses both injected transport and a real loopback fixture HTTP server. The game and browser smoke test run without a model or external network.

Not covered: full assistive-technology audit, other browser engines, large multi-year endurance runs, all live Wikimedia country mappings, production deployment or multiplayer. All source provenance limitations are documented in DATA.md.

## Environment note

Installing Drogon triggered a Homebrew automatic-update conflict in the existing `gromgit/fuse` tap. That task-started rebase was aborted and its original `main` branch and two local commits restored; its worktree was clean afterward. Drogon remained installed. No tap changes are part of this project.
