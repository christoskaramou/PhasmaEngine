# PhasmaEngine — Agent Instructions

> Identical to `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `HERMES.md`, and `.github/copilot-instructions.md`. If you edit one, sync the other four.

`INSTRUCTIONS.md` is a dispatcher pointing to code, MemPalace, and the wiki — read it to know where to look for what. `docs/wiki/index.md` is the synthesis layer; find pages with `bash docs/wiki/tools/search.sh "query"`.

**Wiki sync:** before finishing a session that changed code a wiki page describes, update the page and run `bash docs/wiki/tools/lint.sh`.

## MemPalace

Primary memory and history (when MCP is configured). Use lowercase `phasmaengine` for new project memories.

- At session start for architecture, handoff, or project-history work, load orientation with `mempalace_status`; use `mempalace wake-up --wing phasmaengine` when the CLI is available, then do targeted `mempalace_search` / `mempalace_kg_query`.
- Search before answering architecture/context/past-decision questions; code and repo docs are still the source of truth for current implementation details.
- Store durable verbatim handoffs, decisions, and discoveries with `mempalace_add_drawer`; journal session outcomes with `mempalace_diary_write`.
- Use the knowledge graph for stable facts and relationships (`mempalace_kg_add` / `mempalace_kg_invalidate`) instead of burying them in prose-only drawers.
- Use taxonomy/graph traversal tools (`mempalace_get_taxonomy`, `mempalace_graph_stats`, `mempalace_traverse`) when orienting across subsystems or reconciling multiple wings/rooms.
- After large instruction/doc/history updates, refresh memory deliberately: dry-run `mempalace mine`, avoid build/generated/vendor trees, then `mempalace compress --wing phasmaengine`.

## Rules — commits & workflow

- **Never commit autonomously.** Stage when asked, but do not run `git commit` unless the user explicitly says "commit" (or equivalent) in that message.
- **Never add `Co-Authored-By` lines** to commits.
- **Work on `master` uncommitted by default** — no worktrees, all changes left as unstaged diffs for the user to review. Exception: long-running carve work on the `DX12` branch may commit; check the active branch.
- **Run `clang-format -i`** on every modified `.cpp` / `.h` after editing.
- **Before writing a commit message, run `git diff --staged`** (or `git diff`). Never write commit text from memory.
- **`gh` CLI is unreliable.** Push the branch and tell the user the PR needs manual creation.

## Rules — session end

When the user signals a session is wrapping ("ready for new session", "we're done"), do this without being asked:

1. Review touched files; remove comments that aren't load-bearing.
2. `clang-format -i` every modified `.cpp` / `.h`.
3. Draft a short imperative commit message (do **not** commit).
4. Produce a paste-ready prompt for the next session: branch + engine commit + unstaged files, current state bullets, priority-ranked next targets, build/test commands.

## Rules — code hygiene

- **No test-only engine code in the tracked repo.** Local-repro hooks (env vars, debug toggles, CTS-only branches) stay local; do not commit them even when inert at runtime. Production-relevant feature work (advertising / gating optional features) is unaffected — that does belong in the engine.
- **`third_party/` is off-limits.** Never edit any `third_party/` directory. If an extension is needed, relocate the crate out of `third_party/` first.

## Rules — tools

- **Use Grep / Glob for code lookups.** The codebase vector store is binary (`.bin` / VSTB) and not directly readable.

## Rules — performance testing

- **Scene:** `scene.load("Scenes/sponza.pescene")`. Do NOT use `load_model_async` — the saved scene has the correct camera setup.
- **Build:** Release for comparisons (Debug optimization is meaningless); Debug only for crash investigation. `tools/perf_cycle.sh Release` to kill, build, relaunch.
- **Present mode:** `rhi.change_present_mode("immediate")`, then verify via `engine.get_metrics().fps`. ~60 / vsync-locked = still FIFO; re-call.
- **Screenshot before changes** for a visual fidelity reference.
- **10 snapshots, 1 second apart** (not 0.5s — frame spikes inflate variance at 0.5s).
- **Compare:** `python3 tools/compare_snapshots.py baseline/ current/`. Baseline is fixed; only `current/` changes.
- **Regression thresholds:** FPS >5% drop AND >1 fps; ms >5% AND >0.5ms; VRAM >50 MB.

## Rules — RHI multi-backend carve smokes (Vulkan side, `DX12` branch)

When verifying RHI carve work (Phase 0/1), the `--api vulkan` smoke must load `sponza.pescene` before measuring liveness. Empty-editor smokes hide regressions in Image/Swapchain seams that only manifest once meshes/materials/shaders flow through the carved seam.

```powershell
echo '{"last_scene":"Assets/Scenes/sponza.pescene"}' > build-ninja-full\Release\Assets\editor_config.json
Remove-Item -Force -ErrorAction SilentlyContinue build-ninja-full\Release\PhasmaEditorModule_*.dll
$proc = Start-Process build-ninja-full\Release\PhasmaEditor.exe -ArgumentList '--api','vulkan' -WorkingDirectory build-ninja-full\Release -PassThru
Start-Sleep 18  # 12s is too short — sponza load + first-frame compile takes ~14s
if ($proc.HasExited) { "EXITED EARLY: code=0x{0:X8}" -f $proc.ExitCode } else { "ALIVE pid=$($proc.Id) after 18s"; Stop-Process -Id $proc.Id -Force }
Get-Content build-ninja-full\Release\PhasmaEngine.log -Tail 8
```

Tail must show `Scene loaded from: Assets/Scenes/sponza.pescene` and a Gbuffer shader Init line. The DX12-side smoke is unaffected (early `PE_ERROR` before scene load) but use the same `editor_config.json` on disk so build-tree state matches.

## Rules — PhasmaWebGPU

- **Read the W3C spec first** before any implementation. Navigate: grep `docs/webgpu-spec-graph.txt` → read section in `docs/webgpu-spec-sections.md` → check `docs/webgpu-spec-validation.md` for validation blocks → check `docs/webgpu-spec-musts.txt` for "must" constraints → read the algorithm at those line numbers in `build/_wgpu_spec/spec.txt` (source of truth, 52K lines). Implementation from header comments or memory produces silent spec violations. For every dictionary member referenced by the spec, honor it — silent ignore is a bug.
- **webgpu-samples ports are 1:1 with upstream.** Render-side must match upstream exactly: all models, all scenes, all procedural textures. Simplification is acceptable only for UI/interactive toggles, never for scene content. Procedural assets (canvas-drawn checkerboards, color grids, stripes) get ported to CPU code — do not replace them with stand-in images. Model loading code (`meshes/teapot.ts`, `primitives.ts`) gets ported as constexpr headers.

## Rules — DX12

- **API conservatism.** When adding multi-backend abstractions, expose only what must be backend-agnostic. Everything else stays private to its backend implementation. The public RHI surface is the contract; the cost of adding to it is high, the cost of leaving it private is low.

## graphify

For any question about this repo's architecture, structure, components, or how to add/modify/find
code, your first action should be `graphify query "<question>"` when `graphify-out/graph.json`
exists. Use `graphify path "<A>" "<B>"` for relationship questions and `graphify explain "<concept>"`
for focused-concept questions. These return a scoped subgraph, usually much smaller than the full
report or raw grep output.

Triggers: "how do I…", "where is…", "what does … do", "add/modify a <component>",
"explain the architecture", or anything that depends on how files or classes relate.

If `graphify-out/wiki/index.md` exists, use it for broad navigation. Read `graphify-out/GRAPH_REPORT.md`
only for broad architecture review or when query/path/explain do not surface enough context. Only read
source files when (a) modifying/debugging specific code, (b) the graph lacks the needed detail, or
(c) the graph is missing or stale.

Type `/graphify` in Copilot Chat to build or update the graph.
