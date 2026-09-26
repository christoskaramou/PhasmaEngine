# Scene Prefabs

Prefabs are runtime scene-subtree assets with the `.peprefab` extension. `Scene::SavePrefab` writes a scoped snapshot of a root node and its descendants: referenced sources, meshes, and nodes are remapped into a standalone JSON document with `asset_type: prefab`, `version: 1`, and `root: 0`. Asset/model paths are serialized relative to the prefab file when possible, just like scene paths are kept portable relative to the scene file.

Mesh entries that omit `textures` inherit the cooked model's material textures. An explicit `textures` object replaces those slots, with an empty object clearing them to defaults. The shared restore helper preserves that distinction for prefab instantiation and scene snapshots; merely referencing a cooked mesh cannot erase its textures.

Scene nodes can carry a lightweight `Component_Prefab` marker through `NodePrefabComponent::path`. Normal `.pescene` save/load and hot-reload snapshots preserve that `prefab` path on the node, while the node's other components and children remain ordinary scene data. `Scene::InstantiatePrefab` restores the subtree and reuses already loaded source models by normalized file path or primitive type/parameters. Each instance has fresh mesh descriptors, owned editable materials, and per-node animation state over resident geometry/textures. Surviving source mesh ranges remain reusable after the original instance is deleted; if no ranges survive, geometry is appended again from the loaded model. Sprite and procedural skinned-strip geometry stays private because those components modify its vertices. Reuse dirties only raster instances/materials, avoiding both another cooked-model upload and a full scene geometry upload; resident material tables are refreshed by the deferred instance rebuild. The `Prefab Instantiate` and `Prefab Source` CPU scopes separate overall construction from source loading/reuse. The instance rebuild also exposes `Scene Storage Buffers`, `Scene Indirect Buffers`, `Scene Image Views`, `Scene Material Table`, and `Scene Mesh Constants` CPU scopes; `Scene Material Reflection` isolates layout reflection within the material-table step.

The editor surface is host-owned. FileBrowser recognizes `.peprefab` files and shows the dedicated prefab icon, Hierarchy drag-drop instantiates a prefab at the root or under the hovered node, right-clicking a hierarchy node can save the whole subtree as a prefab, and dropping a hierarchy node into a FileBrowser folder prompts for a prefab file in that folder. Prefab Viewer is the asset-side editor: it opens `.peprefab` files directly, displays the prefab-internal tree, edits names/enabled state/local transforms, adds/removes/reparents items, authors generated primitive and cooked `.pemesh` mesh references, adds/removes common component payloads, saves the asset, and can instantiate the prefab into the active scene without using the scene hierarchy as the editing surface.

Material layouts are cached on the owning `PassInfoAsset` across instance rebuilds.
`ReflectMaterialLayout` keys that cache with `ShaderCache`'s resolved source content
(including recursive includes, global defines, stage and backend), plus the material
buffer name and annotation. Reloading a pass clears its cached key. The cache retains
one layout per pass asset and still checks source content on rebuild, so shader edits
cannot retain stale field offsets; cache hits avoid creating GPU shaders and reflecting
their resources again. The content hash also guards each material's existing layout copy.
Large material rebuilds pack immutable parameter/texture-index data in at most four
chunks on the Update pool, with distinct output byte arrays. Offsets are assigned in
the original traversal order and GPU uploads happen on the caller after every job
finishes. Small rebuilds remain serial. Packing, reflection and upload scopes separate
their costs. Purely visual leaf meshes can use `node:set_visible()` to update
the render flag without dirtying the instance/material tables. This flag does not
disable child nodes, scripts, physics or animation.

