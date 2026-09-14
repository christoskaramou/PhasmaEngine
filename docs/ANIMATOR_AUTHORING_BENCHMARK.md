# Animator authoring benchmark: Hedgewitch and Farmguard

Date: 2026-09-12. This is the first implementation pass of [the authoring plan](ANIMATOR_AUTHORING_PLAN.md).

## Result and current limit

The benchmark exposed and fixed three concrete sources of editing friction: the command interface could not set hand position and orientation together; retiming poses left release markers behind; and layering an imported clip with a different tick rate used the wrong source clock. The new controls and corrected timing pass native Animator checks. These are tool improvements, not a claim that the replacement animation is finished.

The first tool pass kept the source asset unchanged and imported a reference casting study into a separate scratch model. That imported study was not promoted into ATH. The subsequent authoring pass described below replaces the primary attack and its running derivative with a complete native-posed cast, preserving the original model and leaving the other four runtime clips intact. Christos subsequently reviewed Map 1 Player and preferred it ("much better!"). Separate code review has not been recorded.

## Repeatable saved-source bake

ATH `tools/bake_authored_hero.py` now turns the saved staff source into its declared standing/running pair, keeps the sparse source intact, applies the declared smoothing settings, copies release markers, and refreshes duration, staff-tip muzzle, and running anchor metadata. It stages work on a copy, saves/reopens and validates it, then installs with backups and rollback on write errors. It launches and closes its own FIFO/display-1 Animator and restores settings. Existing Phasma processes must be closed before this offline operation.

The native additions are `timeline.info` (exact clip timing and markers) and `timeline.clip_copy` (complete source-to-selected-target replacement with undo/redo). Complete replacement prevents old attack/recovery keys surviving a shortened source. The running derivative starts from the original run tracks, then receives the new upper-body action; resampling the gait at whole frames lost its original subframe keys and was rejected. Runtime still evaluates the independent base gait. If the action outlasts the authored run track, the unused derivative lower track holds at its endpoint; it is not stretched.

The initial bake completed in approximately five seconds including launch and saved validation. Approved attack/run-attack pose matrices match within 0.000001; source and unrelated clip exports match byte-for-byte. Scratch edits verified reach and a release moved to frame 13 (0.325 seconds), with recovery expanded to 0.95 seconds and shortened to 0.7 seconds. Saved derivatives and computed muzzles follow those changes. Whole-frame endpoints/markers and matching source/gait timeline FPS are currently checked explicitly rather than silently resampled. The subsequent Farmguard pass extends the declared right-hand recipe to sword impacts; general contacts and thrown-weapon recipes remain future work.

## Farmguard sword checkpoint

Farmguard now contains `sword_cleave_source` and `sword_overhead_source`, each with eight sparse poses at 40 FPS, a 0.8-second source duration, and impact at frame 12. Native elbow/hand controls preserve the sword grip; a larger blade arc, torso turn, and small hip transfer give the cut preparation and follow-through. The original idle, gait, mesh streams, rigid face, accessory hierarchy, and Downloads sources are preserved. Standing foot drift from sparse IK interpolation is below 0.002 normalized rig units, a 0.2%-height tolerance; generated trajectories retain their exact-foot check. The alternate and primary share their original ready pose.

The same bake helper generates the four runtime attacks. Sword recipes declare `event: impact`, a bone-local weapon tip, blade radius, and frame offsets around the impact. Nine hit segments are sampled from saved native motion, and reach is refreshed from both attacks. Delaying impact to frame 13 moves the sample window to frames 11–15; invalid windows and mixed release/impact markers reject without changing the saved input. The old staff recipe remains compatible and reproduces the approved Hedgewitch motion.

Map 1 Player captured both versions, standing and independently running, at the actual 0.26-second attack cadence. Live target damage continued while moving. Saved/exported weapon positions agree with native playback to within 0.000001 rig units. The running blade remains inside the sword hit capsule across sampled gait phases. A too-tight close-up was widened and recaptured to show the raised blade in full. The technical checks pass; Christos's visual preference for this Farmguard checkpoint remains pending.

ATH review: `Assets/ArtSources/map1_3d/farmguard_animator/sword_attacks_v2/index.html`. Christos identified the broad blade face leading the v1 strike. The v2 correction rotates the held sword 120 degrees around its length consistently across idle, run, both sources, and derived attacks. Body/hand transforms, weapon origin, timing, and geometry remain unchanged within floating-point tolerance. New Player captures and standing/moving damage checks pass. The original v1 checkpoint remains available for comparison.

Local reproduction: `build/local_checks/farmguard_authoring_20260912/` (`baseline.py`, `author_sword.py`, `bake_checks.py`, `player_sword.py`, `install_sword.py`; correction: `rotate_blade.py`, `player_blade.py`, `install_blade.py`). Each native session used FIFO/display 1, closed its owned application, and restored settings/save state. This pass changes assets and offline bake/validation scripts, with no engine/runtime changes or performance claim. Poacher is next.

Native copy tests cover whole-clip replacement, invalid/self-copy rejection, duration changes, different FPS, and exact undo/redo. Reproduction: engine `build/local_checks/hedgewitch_authoring_20260912/bake_checks.py`; reports live under the generated `bake_checks_*` directory and `build/local_checks/authored_bakes/`. The asset validator now reads authored durations/releases and checks the running upper-body loop relative to the pelvis. No runtime or renderer code was added in this bake pass.

## Native cast checkpoint

The installed `Hedgewitch.pemesh` contains `staff_attack_source`, with eight sparse poses covering ready, gather, drive, release, follow-through, recovery, settle, and ready again. The staff moves farther forward at release, the elbow supports its angle, and the free hand stays quiet. `attack` is a separately baked and smoothed derivative; `run_attack` composes its upper-body channels with the existing gait. This pass adds no engine/runtime code.

The first angle-only candidate failed the wrist review: the hand bent over 100 degrees because the elbow stayed low. Coordinating explicit forearm/hand targets with an outward elbow reduced maximum sampled right-wrist flexion below 28 degrees while retaining the grip and clearing the face. Rotation-only puppet edits hold the existing tail position and can move ancestors; both position and orientation are needed for the intended new pose.

Native Player comparisons use the actual 0.26-second attack cadence, independent run/attack clocks, and the existing pelvis anchor. Captured weapon positions match exported poses within 0.000001 rig units; live moving combat continued to release projectiles and damage the target. The final source passed reach, release-delay, and longer-recovery edit checks with exact undo/redo/save-reopen comparisons. Reach still reports a residual near the arm's limit, so persistent exact-contact controls remain a demonstrated gap.

The native geometry validator now treats declared Animator-authored clips as artistic source rather than requiring them to reproduce the legacy Python choreography or its peak-pelvis timing. Rig, wrist, grip, foot, gait, loop, and muzzle checks remain active. The all-hero generator retains heroes that declare native source ownership. All four shared native-hero Lua checks pass.

Evidence and editable checkpoint: ATH `Assets/ArtSources/map1_3d/hedgewitch_animator/staff_cast_v1/index.html`, with workflow notes, Player videos, original asset snapshot, source/derived exports, poses, and validation reports beside it. Local reproduction adds `author_staff.py`, `player_staff.py`, `check_staff.py`, and `edit_final.py` under the engine benchmark scratch directory. The subsequent repeatable bake above removes the manual derivative/metadata updates. Pretrained AI evaluation remains pending.

## Baseline and action brief

Hedgewitch's staff should lead the cast: a readable preparation, a forward staff-tip drive supported by the torso, a quiet supporting free hand, a release from the staff, and a controlled return to ready. Review the entire action at gameplay cadence before inspecting slow motion. Timing is an artistic decision, not a shared formula for every joint.

The preserved native baseline contains `attack` and `run_attack`, each 0.8 seconds with release at frame 12 on the 40 FPS timeline. Front, side, and game-like camera contact sheets were captured at nine poses per clip. Playback advancement was verified in Animator. This pass did not establish a new Player comparison or actual game-camera calibration.

The rig has 19 bones. The staff is already parented to `hand.R`; its grip can be preserved by solving the hand. The face is rigidly carried by the body. The arms are short, so reaching with the hand alone hits a real limit and needs deliberate torso participation. Separate staff and cape pieces have offset bind positions compensated by the authored local channels: a saved ready pose must explicitly include all bones. No geometry, skin-weight, or hierarchy edit was justified or applied in this pass.

Median command round-trip time was approximately 24 ms, with p95 approximately 29 ms in the baseline run. This measures command transport and execution, not total authoring time. The current bottleneck is deciding and controlling coordinated poses, not evidence of a slow command transport. Generic motion analysis also reported thousands of redundant baked keys; its jitter/seam flags are inspection prompts, not counts of visible defects.

## Focused fixes and evidence

| Operation | Before | After / native check |
| --- | --- | --- |
| Change held staff reach/orientation | Grabbing the weapon itself changed its transform relative to the hand by up to 0.723 in the matrix comparison. The command could only supply position. | `timeline.pose_state` exposes the evaluated rig pose. `timeline.grab` accepts position, orientation, or both. Driving `hand.R` preserved the staff grip to floating-point roundoff, kept a pinned body fixed, and undid exactly. A near-limit positional target retained a reported 0.0079 rig-unit gap; this is not an exact contact solver. |
| Delay release | Scaling frames 0–12 by 1.25 moved the pose to frame 15 but left its marker at 12. | Pose and release marker both move to 15. Coincident distinct markers survive save/reopen; sliding and undo restore them. Malformed pose arguments fail without changing the pose. |
| Change recovery | Interval 12–32 could be compressed to 12–27. | Both shortening and lengthening work. Extending the clip to frame 40 before scaling recovery by 1.25 moves its end to 37, leaves preparation unchanged and release at 12, and undoes exactly. Retimed recovery matches the original poses within 0.000001 matrix error. The final candidate's visual recovery remains to be reviewed. |
| Layer imported reference | Target ticks were treated as source ticks, changing playback speed when rates differed. | Four native cases cover differing rates, equal rates, positive offset with wrapping/nonzero start, and negative offset. Across 640 selected-bone samples, local rotation matrix error stayed below 0.000001 versus source playback at the corresponding seconds. Unselected transforms and undo were preserved. |

The position/orientation control uses the existing viewport solver, and marker retiming is fixed in the shared UI/command helper. No additional runtime solver, inference dependency, transport, or asset format was added.

Directly manipulating a weapon remains a free prop edit. Persistent grip/contact spans and a weapon-tip control are still prospective work; hand driving is the verified approach available now.

## Motion reference and AI status

The scratch study uses Quaternius's CC0 [Universal Animation Library](https://quaternius.com/packs/universalanimationlibrary.html), obtained as glTF from [J-Ponzo's public mirror](https://github.com/J-Ponzo/gltf-universal-animation-library). Source files, license, URLs, and SHA-256 hashes are retained in the local reference directory. `Spell_Simple_Shoot` is an authored motion reference, not an AI-generated result. A motion-only derivative mirrors the left-hand cast onto the staff hand, maps corresponding limb names, and stretches 0.5 seconds to 0.8 seconds. The derivative is for animation import, not a mirrored source-mesh preview. Props and clothing retain the native ready-pose channels.

No pretrained model has been run or installed. The inspected machine has a 16 GB RTX 4080 SUPER. HY-Motion's documented 24/26 GB baseline configurations exceed that capacity; that does not establish that every offloaded configuration is impossible. [MoCapAnything's custom-rig route](https://github.com/phongdaot/MocapAnything) remains a trial candidate requiring a suitable video, rig preprocessing, and license checks for its additional dependencies. No quality or time-saving claim has been established for either route on ATH.

## Reproduction and next actions

Local evidence and repro scripts are under `build/local_checks/hedgewitch_authoring_20260912/` in the engine checkout. They are ignored local checks, not tracked test-only engine hooks. They launch their own Animator with FIFO on display 1, operate on copied assets, close it in `finally`, and restore temporary Animator settings.

- `baseline/manifest.json`: original asset hashes and repository HEADs. `baseline/report.json` and contact sheets preserve the comparison.
- `friction.py --after` and `friction_after/report.json`: grip, timing, invalid-input, undo, and save/reopen checks.
- `study.py` and `study/`: experimental native reference import, source/derivative provenance, exported clips, and contact sheets. This script recreates the scratch draft; it must not be used to overwrite a manually improved draft.
- `layer_check.py` and `layer_check/report.json`: source-clock/offset comparisons, unselected-channel preservation, orientation-only editing, and undo.
- `quick_check.py`: broader validator with FIFO/display 1 and a backup/restore wrapper for settings it touches.

Release Animator and Editor builds succeeded. Native checks cover the operations above, and the broader quick validator reports 11 passes with no failures after rebuilding a stale Editor host/module pair that initially failed to load. Format, whitespace, wiki lint, and graph update completed. No Player or performance comparison was run in this pass. The preserved Hedgewitch asset hashes still match, and no Editor, Player, Animator, or Profiler process remained after testing.

At the end of the first tool pass, the next action was native pose blocking. The subsequent checkpoint completes that action and its running/edit demonstrations; Christos's Player review establishes visual preference, and the repeatable bake above removes the manual export/metadata steps. The staff_sweep_v1 checkpoint adds the diagonal alternate and its running combination, preserving the approved primary. Both sources are declared in authoring.sources and selected with the bake helper --source option. Farmguard's sword_attacks_v1 now uses the same workflow for cleave and overhead pairs; native Player and geometry checks pass. Poacher is next. A separate code review remains useful. Trial one pretrained route before deciding whether it deserves integration. Total time to a preferred result is not yet established; the discarded posing and capture attempts are part of the authoring cost.

Ranger remains locked, Thresher unchanged, and Farmer paused. Original ATH assets and unrelated work are preserved. This pass changes Animator authoring operations; it makes no new runtime performance claim.

## Farmguard shield and additional diagonals — 2026-09-12

The user clarified exactly two additional cuts: one diagonal from above with the sword a little outward, and one rising diagonal. Both are authored as separate sparse native sources and baked into standing/running pairs. The accepted cleave, overhead cut, idle, run, blade grip, original mesh streams, and 18-bone rig are preserved. A 584-triangle buckler is rigidly skinned to `hand.L`, adding one mesh/material and no bones or textures. ATH's controller now reads the optional `attack_clips` list; other heroes retain their existing two-attack default. Farmguard cycles four attacks at the unchanged gameplay interval.

Checkpoint: ATH `Assets/ArtSources/map1_3d/farmguard_animator/sword_shield_v3/`. It includes eight Player clips, sparse source exports, pose libraries, rig/skin validation, per-variant real standing/moving damage, locomotion-transition tests, and export/native weapon-position comparisons. The first diagonal draft missed the central test target; the final visible blade path was corrected before regenerating the melee capsules. No capsule-only reach inflation was used.

Ten Release Player profiler snapshots per version were collected at least one second apart, FIFO/display 1, with 80 enemies on Map 1. Mean GPU time was 1.424 -> 1.423 ms, animation scope 0.109 -> 0.124 ms, and reported GPU allocation 778 -> 778 MB. The standard comparison reports no threshold regressions. FIFO frame/CPU totals include presentation waiting; these results establish this bounded check, not an optimization claim. Player profiling uses the native stream with explicit `PE_PROFILER=9876`; the Player MCP surface does not expose the editor-only `profiler_snapshot` tool. Owned applications were closed and temporary settings/save state restored.
