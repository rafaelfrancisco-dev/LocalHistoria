# Model benchmark results

Machine: M1 Pro, 16 GB; context 32768. Windows results must be kept separately.

| Model | Gate | Fixed valid | Expected | Median scenario s | p90 scenario s | Full turns | Median full s |
|---|---|---:|---:|---:|---:|---:|---:|
| baseline | True | 0/0 | 0/0 | None | None | 0 | None |
| qwen2-q4 | True | 0/0 | 0/0 | None | None | 0 | None |
| gemma-e2b-q4 | True | 0/0 | 0/0 | None | None | 0 | None |
| qwen2-q6 | True | 0/0 | 0/0 | None | None | 0 | None |
| lfm-thinking-q4 | False | 0/0 | 0/0 | None | None | 0 | None |

No automatic winner declaration: every scenario must match baseline, every campaign must pass replay and semantic review, and full-turn median/p90/category thresholds must all pass. Incomplete samples never count as acceptance.

Raw timing, token counts, retries, truncations, per-scenario outcomes: `results.json`. Detailed semantic review input: `quality-review.json`. Server logs, props, memory samples, requests/responses, SQLite backups and replay exports are retained in the corresponding run folders.
