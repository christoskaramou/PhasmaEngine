#!/usr/bin/env python3
"""MiniGameKit schema helpers — the single source of truth for the .pescene /
.peprefab JSON that tools/new_game.py emits and for the canonical prefab pack.

The PhasmaEngine scene format is JSON with top-level: settings, sources, meshes,
nodes, lights, cameras, active_camera, scene_scripts. Matrices are 16 floats in
COLUMN-MAJOR order (translation in elements 12,13,14). A primitive source is
{primitive_type, primitive_params}; a mesh references it via {source, source_mesh}
and gets a default material (material_factors/textures are optional). A directional
light is authored as a NODE (component_flags 2 + light{}); the loader prefers the
embedded node light over the top-level lights[] array, so we never double-light.

Run `python tools/minigamekit.py --regen` to (re)write the canonical prefab pack
under Phasma/Runtime/RuntimeAssets/Prefabs and a reference topdown_arena.pescene
under tools/templates/.
"""

import argparse
import json
import os

# --- repo layout -----------------------------------------------------------

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ENGINE_ROOT = os.path.dirname(TOOLS_DIR)
RUNTIME_ASSETS = os.path.join(ENGINE_ROOT, "Phasma", "Runtime", "RuntimeAssets")
GAMEKIT_SRC = os.path.join(RUNTIME_ASSETS, "Scripts", "gamekit")
PREFABS_SRC = os.path.join(RUNTIME_ASSETS, "Prefabs")
TEMPLATES_DIR = os.path.join(TOOLS_DIR, "templates")

# Component flag bitmask (mirrors Component_* in the engine).
FLAG_MESH = 1
FLAG_LIGHT = 2
FLAG_CAMERA = 8
FLAG_SCRIPT = 16
FLAG_RUNTIME_UI = 512

PARK_Y = -1000.0  # offscreen park spot for disabled pool members (gamekit/pool.lua)

# --- matrix helpers (column-major) -----------------------------------------


def mat_scale_translate(sx, sy, sz, tx, ty, tz):
    """A non-rotated scale+translate matrix as 16 column-major floats."""
    return [
        sx, 0.0, 0.0, 0.0,
        0.0, sy, 0.0, 0.0,
        0.0, 0.0, sz, 0.0,
        tx, ty, tz, 1.0,
    ]


def mat_translate(tx, ty, tz):
    return mat_scale_translate(1.0, 1.0, 1.0, tx, ty, tz)


# A downward sun rotation (copied from a known-good authored directional light:
# ~89 deg pitched, light pointing down). Translation is irrelevant for a
# directional light; we keep it well above the arena.
def mat_sun(ty=30.0):
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, -0.001745, -0.999997, 0.0,
        0.0, 0.999997, -0.001745, 0.0,
        0.0, ty, 0.0, 1.0,
    ]


# --- scene settings --------------------------------------------------------


def default_settings():
    """A sane deferred-renderer settings block (from a working scene), tuned for
    a clean mini-game look: full render scale, no editor grid, IBL ambient on."""
    return {
        "right_handed": False,
        "reverse_depth": True,
        "frustum_culling": True,
        "shadows": True,
        "shadow_map_size": 2048,
        "num_cascades": 4,
        "shadow_distance": 250.0,
        "shadow_cascade_lambda": 0.85,
        "shadow_normal_bias": 1.5,
        "shadow_fade_fraction": 0.15,
        "shadow_filter_radius": 0.75,
        "shadow_debug_mode": 0,
        "render_scale": 1.0,
        "ssao": True,
        "fxaa": False,
        "taa": True,
        "cas_sharpening": True,
        "cas_sharpness": 0.5,
        "ssr": False,
        "tonemapping": False,
        "color_grading": False,
        "dof": False,
        "bloom": False,
        "motion_blur": False,
        "IBL": True,
        "IBL_intensity": 1.0,
        "lights_intensity": 1.0,
        "randomize_lights": False,
        "skybox_path": "",
        "time_scale": 1.0,
        "model_list": [],
        "freeze_frustum_culling": False,
        "draw_aabbs": False,
        "draw_grid": False,
        "aabbs_depth_aware": True,
        "dynamic_rendering": True,
        "ray_tracing_support": True,
        "render_mode": 1,
        "use_Disney_PBR": True,
        "physical_point_falloff": False,
        "present_mode": 2,
        "scene_view_aspect_mode": 3,
    }


# --- node builders ---------------------------------------------------------


def camera_node(name, pos, euler, ortho_size):
    return {
        "name": name,
        "parent": -1,
        "local_matrix": mat_translate(*pos),
        "component_flags": FLAG_CAMERA,
        "camera": {
            "projection": "orthographic",
            "fovx": 1.518435,
            "orthographic_size": ortho_size,
            "near_plane": 0.1,
            "far_plane": 640.0,
            "speed": 3.5,
            "euler": list(euler),
        },
    }


def light_node(name, color):
    return {
        "name": name,
        "parent": -1,
        "local_matrix": mat_sun(),
        "component_flags": FLAG_LIGHT,
        "light": {"type": "directional", "color": list(color)},
    }


def mesh_node(name, mesh_index, scale, pos, enabled=True):
    node = {
        "name": name,
        "parent": -1,
        "local_matrix": mat_scale_translate(scale[0], scale[1], scale[2], pos[0], pos[1], pos[2]),
        "component_flags": FLAG_MESH,
        "mesh": mesh_index,
    }
    if not enabled:
        node["enabled"] = False
    return node


def ui_text_node(name, parent_index, text, x, y, w, h, font_scale=2.2,
                 text_color=(0.95, 0.97, 1.0, 1.0), align_h=1):
    return {
        "name": name,
        "parent": parent_index,
        "local_matrix": mat_scale_translate(w, h, 1.0, x, y, 0.0),
        "component_flags": FLAG_RUNTIME_UI,
        "runtime_ui": {
            "type": "text",
            "screen": "__scene_ui",
            "id": name.lower(),
            "body": text,
            "action": "click",
            "fill": [0.0, 0.0, 0.0, 0.0],
            "border": [0.0, 0.0, 0.0, 0.0],
            "accent": [0.0, 0.0, 0.0, 0.0],
            "text_color": list(text_color),
            "image_tint": [1.0, 1.0, 1.0, 1.0],
            "anchor": [0.0, 0.0],  # top-left screen origin -> resolution independent
            "pivot": [0.0, 0.0],   # translation is the widget's top-left corner
            "font_scale": font_scale,
            "text_align_h": align_h,  # 1 = left
            "text_align_v": 2,        # 2 = middle
            "text_offset": [0.0, 0.0],
            "visible": True,
            "draggable": False,
            "no_input": True,
            "bring_to_front": False,
        },
    }


def group_node(name):
    return {"name": name, "parent": -1, "local_matrix": mat_translate(0.0, 0.0, 0.0)}


# --- topdown arena scene ---------------------------------------------------


def build_topdown_scene(on_play_script):
    """Build the topdown_arena scene dict.

    on_play_script: the project-root-relative entry script path that the scene
    runs on play, e.g. "Assets/Scripts/mygame.lua".

    Layout: angled orthographic follow-ready camera, one directional sun, a large
    flat floor, a player sphere, an authored disabled pool of 32 Enemy cubes and 64
    Shot cubes parked offscreen, and a top-left HUD (HP bar + wave label).
    """
    sources = [
        {"primitive_type": "cube", "primitive_params": [1.0]},    # 0: floor/enemy/shot
        {"primitive_type": "sphere", "primitive_params": [0.5]},  # 1: player/pickup-ish
    ]
    meshes = [
        {"source": 0, "source_mesh": 0},  # 0: cube
        {"source": 1, "source_mesh": 0},  # 1: sphere
    ]

    nodes = []
    # 0: camera (angled top-down ortho; gamekit camera.topdown refines the follow)
    nodes.append(camera_node("Camera_0", (0.0, 42.0, 12.0), (1.30, 3.14159, 0.0), 22.0))
    # 1: sun
    nodes.append(light_node("Sun", (1.0, 0.97, 0.9, 2.6)))
    # 2: floor (flat cube)
    nodes.append(mesh_node("Floor", 0, (44.0, 0.4, 44.0), (0.0, -0.2, 0.0)))
    # 3: player (sphere on the ground)
    nodes.append(mesh_node("Player", 1, (1.0, 1.0, 1.0), (0.0, 0.5, 0.0)))

    # Enemy pool: 32 disabled cubes parked offscreen.
    for i in range(1, 33):
        nodes.append(mesh_node("Enemy_%03d" % i, 0, (0.9, 0.9, 0.9), (0.0, PARK_Y, 0.0), enabled=False))
    # Shot pool: 64 disabled small cubes parked offscreen.
    for i in range(1, 65):
        nodes.append(mesh_node("Shot_%03d" % i, 0, (0.25, 0.25, 0.25), (0.0, PARK_Y, 0.0), enabled=False))

    hud_index = len(nodes)
    nodes.append(group_node("HUD"))
    nodes.append(ui_text_node("HUD_HP", hud_index, "HP", 40.0, 40.0, 380.0, 44.0,
                              font_scale=2.2, text_color=(0.4, 1.0, 0.45, 1.0)))
    nodes.append(ui_text_node("HUD_Wave", hud_index, "WAVE 1", 40.0, 92.0, 380.0, 44.0,
                              font_scale=2.6, text_color=(0.95, 0.85, 0.35, 1.0)))

    return {
        "settings": default_settings(),
        "sources": sources,
        "meshes": meshes,
        "nodes": nodes,
        "lights": [],
        "cameras": [
            {
                "name": "Camera_0",
                "position": [0.0, 42.0, 12.0],
                "euler": [1.30, 3.14159, 0.0],
                "fovx": 1.518435,
                "projection": "orthographic",
                "orthographic_size": 22.0,
                "near_plane": 0.1,
                "far_plane": 640.0,
                "speed": 3.5,
                "node_index": 0,
            }
        ],
        "active_camera": 0,
        "scene_scripts": {"on_play": [on_play_script]},
    }


# --- prefab pack -----------------------------------------------------------

# name -> (primitive_type, params, uniform_scale). Each prefab is a single root
# node with one primitive mesh and a default material.
PREFAB_SPECS = [
    ("actor", "sphere", [0.5], 1.0),
    ("projectile", "cube", [0.3], 1.0),
    ("pickup", "sphere", [0.35], 1.0),
    ("trigger", "cube", [1.0], 1.0),
    ("health_bar", "quad", [1.0, 0.15], 1.0),
    ("selection_ring", "torus", [0.6, 0.08], 1.0),
    ("card", "quad", [1.0, 1.4], 1.0),
]


def build_prefab(name, primitive_type, params, scale):
    pretty = "".join(part.capitalize() for part in name.split("_"))
    return {
        "asset_type": "prefab",
        "version": 1,
        "root": 0,
        "sources": [{"primitive_type": primitive_type, "primitive_params": params}],
        "meshes": [{"source": 0, "source_mesh": 0}],
        "nodes": [
            {
                "name": pretty,
                "parent": -1,
                "local_matrix": mat_scale_translate(scale, scale, scale, 0.0, 0.0, 0.0),
                "component_flags": FLAG_MESH,
                "mesh": 0,
            }
        ],
    }


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=4)
        f.write("\n")


def regen():
    # Canonical prefab pack.
    for name, ptype, params, scale in PREFAB_SPECS:
        out = os.path.join(PREFABS_SRC, name + ".peprefab")
        write_json(out, build_prefab(name, ptype, params, scale))
        print("wrote", out)
    # Reference scene (placeholder entry script path).
    ref = os.path.join(TEMPLATES_DIR, "topdown_arena.pescene")
    write_json(ref, build_topdown_scene("Assets/Scripts/game.lua"))
    print("wrote", ref)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MiniGameKit schema tools")
    parser.add_argument("--regen", action="store_true",
                        help="(re)write the canonical prefab pack + reference scene")
    args = parser.parse_args()
    if args.regen:
        regen()
    else:
        parser.print_help()
