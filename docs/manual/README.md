# PhasmaEngine manual

This is the user guide for the programs that ship with the engine. Each page says what the program is for, how to start it, and what each control does.

| Program | What it is |
|---|---|
| [Launcher](launcher.md) | Picks the project, the startup scene, and which program to run. |
| [Editor](editor.md) | Builds scenes: hierarchy, viewport, materials, scripts, terrain, voxels, play mode. |
| [Player](player.md) | Runs a project with no editor UI. Also covers export and the game driver. |
| [Animator](animator.md) | Rigs and animates a cooked `.pemesh` in its own window. |
| [Profiler](profiler.md) | Live frame, CPU, GPU, and memory view of a running Player. |
| [Cook](cook.md) | Turns glTF, FBX, OBJ, and the other source models into `.pemesh`. |
| [Scripting](scripting.md) | Lua scripts: where they run, the lifecycle, and the tables a game calls. |

The engine checkout does not contain the sample games. Clone [PhasmaProjects](https://github.com/christoskaramou/PhasmaProjects) beside this repo and open a project from the launcher. The repository [README](../../README.md) has the build and graphics-API notes.

Screenshots in this manual were taken from the programs in `build-ninja-physics/Release`.
