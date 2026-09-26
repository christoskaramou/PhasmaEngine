# Project knowledge

Verified: 2026-09-20. The user selected the wiki as the sole maintained project knowledge system.

## Where to look

- Current behavior: search live source, then inspect definitions and callers.
- Architecture, decisions and pitfalls: use [the wiki index](index.md) and focused section searches.
- Personal preferences and agent behavior: global/project instructions. Do not copy them into subsystem pages.

Record a durable fact once, in the relevant subsystem page. Include its source path, reason and verification date. Distinguish current behavior, accepted intent and unimplemented proposals. Supersede old claims explicitly; do not accumulate contradictory updates. Preserve material caveats and failed approaches only when they prevent repeating a mistake.

Do not add routine session transcripts, build-success diaries, copied source listings, generated/vendor documentation, model evaluations or temporary next-task lists. A larger wiki is not a better wiki. Search results are navigation aids, not proof of correctness.

## Retired systems and recovery

MemPalace, Graphify and automatic Codex project-memory generation are disabled in the configured local workflow. Do not query, update or recreate these stores by default. Source search and optional Jev relevance ranking are retrieval tools, not additional maintained knowledge stores.

The local recovery archive is `C:/Users/Christos/.agents/knowledge-archive-20260920`. It contains all 5,818 exported MemPalace drawers, consistent SQLite backups of the drawer and knowledge-graph databases, original configuration files, retired graph outputs/skills and the prior wiki chronology. Original databases remain available for recovery; they are not active knowledge sources. The archive is machine-local and must not be published as project documentation.

Only retrieve archived material when explicitly recovering history. Check its date and verify source claims before promoting it into the wiki. Do not bulk-import the archive back into default search.

## Project boundaries

PE engine contracts belong here. AgainstTheHero gameplay/art decisions belong in that project's wiki. PhasmaProjects is a separate root; do not substitute a historical ATH checkout for `C:/Users/Christos/repos/AgainstTheHero`.

The Router's project-context helper currently searches PE only. Other projects use their own wiki and direct source search until explicit support is implemented.
