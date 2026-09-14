# Phasma Animator authoring plan

Date: 2026-09-12

Status: Started on 2026-09-12 following Christos's approval. Christos reviewed Hedgewitch in Map 1 Player and preferred the result ("much better!"). The primary attack, running composition, and three local-edit demonstrations establish the first visual benchmark. The saved-source bake is now one repeatable command, verified against that approved motion and with altered reach/release/recovery. Details are in [the authoring benchmark](ANIMATOR_AUTHORING_BENCHMARK.md). Hedgewitch's alternate and Farmguard's cleave/overhead pairs are installed with native Player comparisons ready for review. Poacher is the next hero. Pretrained-model evaluation and the wider hero rollout remain pending; no purchases or custom model training are approved by this roadmap.

## Outcome

Update: Farmguard's diagonal cleave, overhead cut, and running combinations are installed and ready for visual review at ATH `Assets/ArtSources/map1_3d/farmguard_animator/sword_attacks_v2/index.html`. Christos identified a flat-side slap in v1; v2 rolls the held blade so its edge leads, retaining the body motion and timing. Both retain separate sparse native sources, supported elbow/blade angles, torso participation, hip transfer, and shared ready poses. The bake supports impact events and derives sword hit samples from saved motion around that event; delay and invalid-event cases pass. Native Player standing/moving damage and exported-pose agreement pass. Gait, geometry, and the Hedgewitch checkpoint are preserved. Poacher is next; the corrected blade angle is ready for review.

Create expressive, coordinated hero animation through direct posing, useful motion references, and quick visual iteration. Both Christos and the agent should be able to change reach, timing, grip, and recovery without rewriting a Python choreography script or rebuilding every clip.

Prove this on one Hedgewitch staff attack, then apply the proven workflow to the four remaining heroes. Evaluate pretrained motion assistance during the proof. Invest in integration only if it improves the final animation or substantially reduces authoring and cleanup effort.

## Diagnosis and existing foundation

The current workflow generates detailed bone trajectories numerically and bakes many samples. This can produce smooth, valid curves without convincing intent, weight transfer, anticipation, or coordination. Fixing interpolation alone will not solve that problem. Weapon motion, hands, torso, and support feet need to belong to the same action.

Animator already has viewport posing, IK, pins/locks, balance helpers, a pose library, reference sequences, motion trails, interval editing, clip layering/baking, and BVH/glTF exchange. The plan extends those existing surfaces. The gaps to prove are persistent action controls and contacts, reusable rig mapping, preservation of editable source, and a fast review loop that matches gameplay.

The recent moving-attack anchor correction is useful runtime groundwork. Technical correctness checks on that correction do not establish artistic quality. Existing clips remain comparison material.

Current implementation entry points:

- `Phasma/Animator/Code/AnimationTimeline.cpp`: timeline actions, pose library, markers, interval authoring.
- `Phasma/Animator/Code/Animation/AnimationPoseViewport.cpp`: posing, IK, locks, balance, references.
- `Phasma/Animator/Code/Animation/AnimationClipTools.h`: gait, interval operations, clip composition.
- `Phasma/Animator/Code/Animation/ClipExchange.cpp`: import, bind-space/name-based retargeting, export.
- `tools/animator_client.py`: existing batched command transport for the agent.
- ATH `tools/build_remaining_heroes_animator.py`: current procedural source and dense baking workflow to replace for ordinary artistic edits.

These are investigation targets, not a commitment to change every file.

## Success criteria

| Goal | Evidence needed |
| --- | --- |
| Convincing motion | Preferred before/after playback at gameplay scale: clear action, coordinated body and weapon, deliberate rhythm, believable support and recovery. |
| Easy editing | Change reach, release timing, and recovery independently; retain grips and unaffected poses; save, reload, and undo successfully. |
| Faster iteration | Measure time to first usable result, time per local correction, and number of secondary repairs. Improve against the recorded baseline. |
| Human and agent parity | The same meaningful operations are available through the existing UI and command interface, with inspectable state and reversible edits. |
| Gameplay correctness | Preview and Player agree on release/impact events, facing, root motion, blends, and independent running/attacking clocks. |
| Sustainable production | Reusable rig profiles and motion examples work on another hero; Release measurements remain within the project's regression limits. |

Record quality and technical results separately. A clean validator result cannot mark an animation as visually successful. Numerical clearance tolerances should follow the asset scale and intended contact; universal pose or timing limits are inappropriate.

## Phase 1 — Establish the benchmark and inspect the rig

**Goal:** Make the actual bottlenecks visible before building features.

Actions:

1. Preserve current source assets, authored clips, and working changes. Record the exact asset and engine revisions used for comparisons.
2. Select Hedgewitch's primary staff attack as the benchmark. Define the intended action: staff initiates the attack, the torso supports the reach, the free hand supports the gesture, and recovery returns to a usable stance.
3. Capture the current attack standing and running, at actual gameplay cadence, using the game camera plus front and side views. Keep a fixed target and repeatable starting state.
4. Inspect bind/rest pose, axes, scale, hierarchy, joint placement, skin weights, rigid accessories, grip position, and reach. Correct only verified rig defects that prevent the action. Distinguish bad deformation from bad motion.
5. Time three representative corrections: change staff reach, move the release moment, and lengthen recovery. Record tool latency, editing effort, and unintended changes.
6. Choose a performed video reference or a compatible licensed motion example. Identify its preparation, drive, release/impact, follow-through, and recovery. Adapt it to ATH's proportions and readable game silhouette.

**Deliverable:** Reproducible baseline captures, a concise action brief, a rig defect list, and an iteration-cost record.

**Done when:** We can identify whether each visible failure comes from rigging, action design, timing, or runtime composition, and can repeat the comparison.

## Phase 2 — Prove the motion workflow with existing tools

**Goal:** Produce one substantially better attack before committing to a large feature build.

Actions:

1. Block the action with a small set of strong poses. Four to six is a useful starting point, not a fixed requirement. Judge silhouette and intent before adding inbetweens.
2. Establish the staff-tip path and grip, then coordinate hands, elbows, torso, pelvis, and support. Use existing IK, locks, pose library, reference playback, and motion trails where they work.
3. Shape anticipation, acceleration, release, follow-through, and recovery deliberately. Use overlap where it serves the action; avoid applying the same timing curve to every joint.
4. Make a complete reference-guided candidate, including its running combination. Log every awkward operation or missing capability encountered.
5. Evaluate one pretrained motion route against that candidate on the same action and rig. Check model access, hardware, export compatibility, and applicable licensing before running it. Count acquisition, retargeting, and cleanup in the comparison.
6. Compare baseline, reference-guided candidate, and AI-assisted candidate at identical cadence and camera. Assess character, weapon ownership, contacts, and total editing effort.

**Deliverable:** One preferred Hedgewitch attack, standing and running comparisons, and a ranked list of demonstrated authoring gaps.

**Decision:** Continue with reference-guided posing if AI adds more cleanup than value. Integrate a pretrained route only if the trial supports it. If an existing Animator limitation blocks the proof, implement the smallest relevant slice from phases 3–5 and repeat the benchmark.

### Pretrained candidates and useful precedents

- [MoCapAnything V2](https://animotionlab.github.io/MoCapAnythingV2/) is a candidate for deriving motion from video with a supplied skeleton. Its available [repository](https://github.com/phongdaot/MocapAnything) identifies itself as a clean reimplementation rather than the original experimental code. ATH compatibility and prop handling require a practical trial.
- [HY-Motion 1.0](https://github.com/Tencent-Hunyuan/HY-Motion-1.0) is a candidate for text-conditioned human skeletal motion. Its documented hardware requirements, human-motion assumptions, and limitations around in-place/seamless loops make retargeting and cleanup part of the evaluation.
- [Cascadeur AutoPosing](https://cascadeur.com/help/tools/animation_tools/autoposing) and [inbetweening](https://cascadeur.com/help/category/278) demonstrate the value of pose completion and pose-conditioned motion. They are workflow references or optional trial tools, not a decision to purchase software or reproduce an entire application.

Test one suitable route first. These tools are candidates, not verified ATH solutions. Training a custom model would become a separate proposal only after reusable rigs, curated preferred clips, and a measured limitation of available approaches exist.

## Phase 3 — Make controls express the action

**Goal:** Manipulate the staff, hands, and support naturally, with the rest of the rig following coherently.

Actions, implemented in the order justified by phase 2:

1. Extend rig presets with explicit semantic roles, forward/up axes, rest-pose conventions, limb chains, and elbow/knee bend preferences. Reuse that mapping for controls and retargeting.
2. Define weapon grip frames and a weapon-tip control. Decide which object drives each action so a weapon target and a hand target do not fight each other.
3. Add editable time spans for grip and foot contacts, with intentional attachment/release moments. Preserve world- or character-relative contacts as appropriate and blend changes without popping.
4. Make elbow placement and reach limits visible. Build on current IK, pins, and balance helpers; add torso/pelvis participation only where the current solver demonstrably fails.
5. Expose hand, weapon-tip, and foot trajectories with contact and target overlays. Use clearance warnings to direct inspection rather than automatically removing intentional contact.
6. Keep controls in the existing Rig/Animate surface and expose the same operations to the existing agent command interface.

**Deliverable:** A reusable action-control profile, first proven on Hedgewitch and then on one different hero rig.

**Done when:** Moving the staff tip preserves the grip, elbows bend sensibly, planted support stays stable, and a local edit does not require manually repairing the whole body.

## Phase 4 — Preserve editable poses and timing

**Goal:** Keep artistic decisions editable throughout production.

Actions:

1. Preserve source poses, control targets, contact spans, and action markers separately from the final dense runtime bake. Use the smallest extension to existing persistence that supports the benchmark.
2. Reuse pose-library and interval tools for anticipation, release/impact, and recovery. Allow independent timing changes while carrying attached contacts and gameplay events correctly.
3. Keep local changes local. Rebaking must preserve unrelated poses and explicit manual overrides; editing a baked derivative must have a clear path back to retained source.
4. Use sparse authored keys where useful, inspect velocity/trajectory continuity, and simplify dense imported motion only within measured error tolerances. Preserve important contacts and event poses.
5. Retain established bake behavior: sampled motion remains Linear, Stepped remains Stepped, and absent curve kinds stay absent. Improving motion must not introduce double easing.
6. Cover edits with existing undo/redo and atomic save behavior. Define and verify backward-compatible loading for any new authoring data.

**Deliverable:** An editable staff attack that can be retimed and rebaked without losing intent or manual corrections.

**Done when:** Reach, release, and recovery can each be changed, undone, saved, and reloaded without repairing unaffected portions of the clip.

## Phase 5 — Close the preview and agent feedback loop

**Goal:** Make a small edit cheap to judge in the conditions where players see it.

Actions:

1. Add a reusable preview setup using the runtime animation evaluator. Include a target, actual game camera, independent run/attack clocks, facing behavior, and release/impact markers.
2. Support repeatable standing, moving, turning, and transitioning scenarios. Include attack interruption and target changes where gameplay allows them.
3. Make before/after comparison, frame stepping, slow playback, looping a selected range, and range capture quick within existing surfaces.
4. Extend existing command batching with the missing semantic edits, state inspection, and capture operations found during the benchmark. Include clear completion/error results and reversible edit groups; retain the current transport unless measurement shows it is a bottleneck.
5. Use the same agent loop every time: inspect source and playback, state the visible problem, change a bounded part, capture the full action plus transitions, compare, then keep or undo.
6. Measure warm edit-to-preview time and total time to a preferred result. Set latency targets from the baseline, then profile only measured bottlenecks.

**Deliverable:** One repeatable human/agent review workflow that shows the actual composed motion without restarting and regenerating the entire hero library for each correction.

**Done when:** Both Christos and the agent can perform the three benchmark corrections and inspect their gameplay result quickly, without bespoke per-hero Python choreography.

## Phase 6 — Productize useful motion assistance

**Goal:** Make a proven motion source reusable, if phase 2 shows a benefit.

Actions:

1. Connect only the winning external generation/import route through existing clip exchange. Preserve the input reference, source clip, model/tool version, and applicable provenance/license information.
2. Reuse explicit rig-role mapping and bind-pose calibration. Handle scale, root motion, and ATH's short-limbed proportions before judging the animation.
3. Apply grip/contact cleanup and action controls after import. Keep results editable using phase 4 rather than treating generated motion as final.
4. If available and beneficial, test constrained inbetweening or pose completion on a selected interval with fixed boundary poses and contact requirements.
5. Keep ordinary authored/baked clips as the runtime output. Measure generation and cleanup effort separately from runtime performance.

**Deliverable:** A reproducible assisted-authoring path, or a documented decision that reference-guided authoring remains more effective for these heroes.

**Done when:** Assistance improves quality or total authoring effort on a second action without relying on unrepeatable manual repairs. This phase is conditional and does not block shipping good manually authored clips.

## Phase 7 — Roll out to the remaining heroes

**Goal:** Create distinct, coordinated move sets using the proven workflow.

| Order | Hero | Actions and visual priorities |
| --- | --- | --- |
| 1 | Hedgewitch / Mage | Finish the staff-led primary attack; create a distinct alternate sweep/thrust; coordinate body support, tip direction, and release. |
| 2 | Farmguard / Warrior | Cleave and overhead cut, plus the requested outward downward diagonal and rising diagonal; rigid left-hand buckler, weight transfer, cutting edge, controlled recovery. The four-attack checkpoint is `farmguard_animator/sword_shield_v3`. |
| 3 | Poacher / Rogue | Right/left dagger throws; preparation, compact accurate release, recoverable stance, rigid accessories that follow the body appropriately. |
| 4 | Gravetender / Necromancer | Curse thrust, rake, and summon; clear casting source, deliberate rhythm, characteristic silhouette, coordinated accessories. |

For each hero:

1. Inspect the rig and reference before editing. Establish one preferred attack first, then its alternate or special action.
2. Author idle and run with deliberate support, stride direction, and loop continuity. Compose run/attack variants with independently timed gait and action, using the established runtime anchor behavior.
3. Test transitions, actual attack cadence, turning and aiming, hand/weapon contacts, clothing clearance, projectile release or melee impact, and return to a usable stance.
4. Review full-speed gameplay first, then use front/side and slow playback to diagnose defects. Add secondary motion only after the main action reads clearly.
5. Save reusable poses, contact setups, and motion examples. Reuse technique while retaining each hero's rhythm and attack identity.

**Deliverable:** Reviewed idle, run, attack, alternate attack, and running combinations for all four heroes, plus Gravetender's summon combinations and an updated comparison gallery.

**Done when:** Each action is visually preferred to its baseline, survives actual runtime composition, and remains practical to edit. Keep Ranger locked, Thresher unchanged, and Farmer paused within this scope.

## Phase 8 — Validate and make the workflow maintainable

**Goal:** Keep quality gains through export, gameplay, and future edits.

Actions:

1. Run focused checks for hierarchy/weights, finite transforms, event timing, contact continuity, loop seams, root motion, and clipping at representative poses. Update old design validators when an intentional new design changes their assumptions.
2. Verify meaningful failure cases in save/reload, undo/redo, import, retarget, bake, export, and interrupted operations. Preserve original assets and unsaved edits.
3. Test authored and runtime-composed playback with different run/attack rates and transitions. Require both technical results and visible playback evidence.
4. For runtime or hot-path changes, compare Release builds against a fixed pre-change baseline. Use FIFO on display 1 as requested, record frame time rather than relying on capped FPS, and take ten snapshots at least one second apart. Apply the project's frame-time and memory regression thresholds.
5. Use representative ATH combat for animation workload; add the saved Sponza scene if a shared renderer/engine change affects that path. Profile before optimizing unrelated subsystems; make each measured optimization a separately validated change.
6. Close all owned Editor, Player, Animator, and Profiler processes after testing. Restore temporary settings and test state. Preserve unrelated work and original sources.
7. Document the repeatable authoring workflow, update affected wiki pages, and run required format/lint/checks when code is changed. Keep local repro hooks outside tracked production engine code. Leave all new work uncommitted unless explicitly requested.

**Deliverable:** A small reusable regression suite, before/after evidence, performance results for affected paths, and a workflow guide usable without this conversation.

**Done when:** Another hero/action can be produced and corrected using the documented workflow, with no unexpected runtime cost or loss of editable work.

## Execution order and first milestone

1. Start phases 1 and 2 using current tools.
2. Implement only the proven blocking slices of phases 3–5; repeat the same staff-attack benchmark after each meaningful change.
3. Finish the first milestone: one visibly preferred, editable Hedgewitch attack, its running composition, and three successful local-edit demonstrations.
4. Make the AI integration decision from the trial; run phase 6 only if warranted.
5. Roll out phase 7 one hero at a time, with phase 8 validation accompanying changes throughout.

The initial milestone is not a new general animation platform. It is a convincing staff attack that Christos and the agent can both modify efficiently. Its measured problems determine which tools deserve further investment.
