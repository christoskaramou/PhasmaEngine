# Code Quality & Performance

Hard rule for all agents and contributors. Full text lives in root `AGENTS.md` under `Rules — code quality & performance (ponytail)`; Cursor loads `.cursor/rules/ponytail-code-quality.mdc` as an always-on reminder.

## Posture

Default is ponytail **full**: the smallest correct change that does not regress performance. The user can switch intensity with `/ponytail lite|ultra`; "stop ponytail" / "normal mode" reverts intensity only — the hard rule (small diff + perf protection) stays.

## The ladder

Stop at the first rung that holds:

1. YAGNI — does this need to exist?
2. Reuse — pattern or helper already in this repo?
3. Stdlib / engine utilities before bespoke code.
4. Platform / RHI primitives before new abstractions or dependencies.
5. Existing dependencies — no new package for a few lines.
6. One line if one line is correct.
7. Minimum diff that fixes the real problem.

Read the full flow end to end before picking a rung. Bug fixes belong at the shared choke point, not as symptom patches in every caller.

## Style

- No unrequested abstractions (single-impl interfaces, one-product factories).
- Deletion over addition; boring over clever; fewest files.
- Deliberate simplifications: `// ponytail:` comment naming the ceiling and upgrade path.

## Performance

Hot-path and render changes must not regress without explicit user approval. Before claiming render or perf work done, follow `AGENTS.md` → `Rules — performance testing` (Sponza scene, Release build, immediate present, 10 snapshots, `tools/compare_snapshots.py` thresholds). Profile before adding caches or speculative complexity.

## Boundaries

Never ponytail away: trust-boundary validation, data-loss prevention, security, accessibility, or anything the user explicitly requested in full.

## Related

- `AGENTS.md` — canonical rule block and perf testing playbook
- `INSTRUCTIONS.md` — dispatcher pointer to this policy
- MemPalace wing `phasmaengine`, room `operating-protocol` — mined instruction updates
