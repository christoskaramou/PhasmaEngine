#include "Voxel/VoxelSystem.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#ifdef PE_PHYSICS
#include "ECS/Context.h"           // GetGlobalSystem
#include "Systems/PhysicsSystem.h" // sim-state edge -> re-register the terrain collider
#endif

namespace pe::voxel
{
    namespace
    {
        uint64_t HashCombine(uint64_t h, uint64_t v)
        {
            return (h ^ v) * 1099511628211ull; // FNV-1a step
        }

        uint64_t HashInt(uint64_t h, int v)
        {
            return HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(v)));
        }

        uint64_t HashString(uint64_t h, const std::string &s)
        {
            h = HashCombine(h, s.size());
            for (char c : s)
                h = HashCombine(h, static_cast<uint64_t>(static_cast<uint8_t>(c)));
            return h;
        }

        // Structural config: any change here recreates the world (lod0Radius retunes live instead).
        uint64_t HashStructuralConfig(const VoxelConfig &cfg)
        {
            uint64_t h = 1469598103934665603ull;
            h = HashInt(h, cfg.loadRadius);
            h = HashInt(h, cfg.unloadMargin);
            h = HashInt(h, cfg.uploadBudgetPerFrame);
            h = HashInt(h, cfg.groundY);
            h = HashInt(h, cfg.worldRadius);
            h = HashInt(h, cfg.boundsCenterCx);
            h = HashInt(h, cfg.boundsCenterCz);
            h = HashCombine(h, cfg.streaming ? 1u : 0u);
            h = HashCombine(h, cfg.smooth ? 1u : 0u);
            h = HashString(h, cfg.saveDir);
            // Worldgen. Floats are hashed quantized — inspector drags step well past 1/16 block.
            h = HashInt(h, static_cast<int>(cfg.noiseAmplitude * 16.0f));
            h = HashInt(h, static_cast<int>(cfg.noiseFeatureScale * 16.0f));
            h = HashInt(h, cfg.noiseSeed);
            h = HashCombine(h, cfg.caves ? 1u : 0u);
            h = HashInt(h, cfg.seaLevel);
            h = HashString(h, cfg.heightmapPath);
            h = HashInt(h, static_cast<int>(cfg.heightMin * 16.0f));
            h = HashInt(h, static_cast<int>(cfg.heightMax * 16.0f));
            h = HashInt(h, static_cast<int>(cfg.groundHeight * 16.0f));
            h = HashInt(h, static_cast<int>(cfg.seaLevelM * 16.0f));
            h = HashString(h, cfg.strata1Path);
            h = HashString(h, cfg.strata2Path);
            h = HashString(h, cfg.featuresPath);
            h = HashInt(h, cfg.blocksPerPixel);
            h = HashInt(h, cfg.surfaceBlock);
            h = HashInt(h, cfg.surfaceBands ? 1 : 0);
            h = HashInt(h, cfg.strata1Block);
            h = HashInt(h, cfg.strata2Block);
            h = HashInt(h, cfg.fillBlock);
            h = HashInt(h, cfg.strata1Thickness);
            h = HashInt(h, cfg.strata2Thickness);
            return h;
        }
    } // namespace

    VoxelSystem::~VoxelSystem()
    {
        Destroy();
    }

    void VoxelSystem::Init(CommandBuffer *)
    {
        SetEnabled(true);
    }

    void VoxelSystem::Update()
    {
        ReconcileComponentWorld();
        if (m_world)
            m_world->Update();
#ifdef PE_PHYSICS
        // The terrain collider registered in edit mode fails to build at StartSimulation because the
        // runtime host node is re-churned on play-enter. Re-register it on the live host on the sim
        // false->true edge (and drop it cleanly on stop). Re-applying the same flag re-cooks it.
        bool sim = false;
        if (auto *ps = GetGlobalSystem<PhysicsSystem>())
            sim = ps->IsSimulating();
        if (sim != m_wasSimulating)
        {
            m_wasSimulating = sim;
            if (m_world)
                m_world->SetPhysicsEnabled(m_world->Config().physics);
        }
#endif
    }

    void VoxelSystem::Destroy()
    {
        if (m_world)
        {
            m_world->Destroy();
            m_world.reset();
        }
        m_componentOwned = false;
        m_appliedHash = 0;
        m_appliedLod0Radius = 0;
        m_appliedPhysics = false;
        m_appliedFriction = 0.5f;
        m_appliedRestitution = 0.3f;
        // Stay enabled: Context::UpdateSystems skips disabled systems, and the component reconcile
        // must keep running to rebuild a Voxel World node's world after play-stop teardown.
    }

    VoxelWorld *VoxelSystem::World()
    {
        return m_world.get();
    }

    VoxelWorld *VoxelSystem::CreateWorld(Scene *scene, const VoxelConfig &cfg)
    {
        if (!m_world)
            m_world = std::make_unique<VoxelWorld>();
        else
            m_world->Destroy();

        m_world->Create(scene, cfg);
        // Callers own the world they create: voxel.create makes a script world (reconcile leaves it
        // alone — a script create even overrides a component world); ReconcileComponentWorld re-flags
        // ownership right after it calls this.
        m_componentOwned = false;
        m_appliedHash = 0;
        SetEnabled(true);
        // Bootstrap registration via CreateGlobalSystem<VoxelSystem>() is wired by PlayerHost.cpp's orchestrator.
        return m_world.get();
    }

    void VoxelSystem::ReconcileComponentWorld()
    {
        Scene *scene = GetActiveScene();
        NodeId *node = scene ? scene->GetVoxelWorldNode() : nullptr;
        NodeVoxelWorldTag *tag = node ? scene->GetVoxelWorldForNode(node) : nullptr;
        const bool want = tag && tag->worldEnabled && scene->IsNodeHierarchyEnabled(node);

        if (!want)
        {
            if (m_world && m_componentOwned)
            {
                m_world->Destroy();
                m_world.reset();
                m_componentOwned = false;
                m_appliedHash = 0;
                m_appliedLod0Radius = 0;
            }
            return;
        }

        // A healthy script-created world (voxel.create) owns the slot — leave it alone. A DEAD one
        // (its arena wiped by a scene rebuild the script never followed up on, e.g. leaving play via
        // engine.set_play_mode which skips the full stop teardown) must not block the component
        // forever — fall through and let the recreate below take over.
        if (m_world && !m_componentOwned && m_world->IsArenaAlive())
            return;

        VoxelConfig cfg{};
        cfg.loadRadius = tag->loadRadius;
        cfg.unloadMargin = tag->unloadMargin;
        cfg.uploadBudgetPerFrame = tag->uploadBudget;
        cfg.groundY = tag->groundY;
        cfg.worldRadius = tag->worldRadius;
        cfg.streaming = tag->streaming;
        cfg.saveDir = tag->saveDir;
        cfg.noiseAmplitude = tag->noiseAmplitude;
        cfg.noiseFeatureScale = tag->noiseFeatureScale;
        cfg.noiseSeed = tag->noiseSeed;
        cfg.caves = tag->caves;
        cfg.seaLevel = tag->seaLevel;
        cfg.heightmapPath = tag->heightmapPath;
        cfg.heightMin = tag->heightMin;
        cfg.heightMax = tag->heightMax;
        cfg.groundHeight = tag->groundHeight;
        cfg.seaLevelM = tag->seaLevelM;
        cfg.strata1Path = tag->strata1Path;
        cfg.strata2Path = tag->strata2Path;
        cfg.featuresPath = tag->featuresPath;
        cfg.blocksPerPixel = tag->blocksPerPixel;
        cfg.smooth = tag->smooth;
        cfg.physics = tag->physics;                       // collider toggle — live-applied below, NOT in the structural hash
        cfg.physicsFriction = tag->physicsFriction;       // collider material — live-applied below
        cfg.physicsRestitution = tag->physicsRestitution; // (no re-cook), NOT in the structural hash
        cfg.surfaceBlock = tag->surfaceBlock;
        cfg.surfaceBands = tag->surfaceBands;
        cfg.strata1Block = tag->strata1Block;
        cfg.strata2Block = tag->strata2Block;
        cfg.fillBlock = tag->fillBlock;
        cfg.strata1Thickness = tag->strata1Thickness;
        cfg.strata2Thickness = tag->strata2Thickness;
        // The node's world position is the volume center. Only bounded / non-streaming worlds depend
        // on it, so dragging the node around an infinite streaming world doesn't churn recreates.
        if (tag->worldRadius > 0 || !tag->streaming)
        {
            const vec3 p = vec3(scene->GetWorldMatrix(node)[3]);
            const ColumnCoord center = WorldToColumn(static_cast<int>(std::floor(p.x)),
                                                     static_cast<int>(std::floor(p.z)));
            cfg.boundsCenterCx = center.cx;
            cfg.boundsCenterCz = center.cz;
        }
        const int lod0Radius = tag->lodEnabled ? std::max(1, tag->lod0Radius) : 0;
        const uint64_t hash = HashStructuralConfig(cfg);

        // Inspector "Rebuild World" button (and Map Painter Save): force an immediate recreate — the
        // only way to pick up a repainted map behind an unchanged path, and the manual trigger when
        // Auto Rebuild is off.
        bool forceRebuild = false;
        if (tag->rebuildRequested)
        {
            tag->rebuildRequested = false;
            forceRebuild = true;
        }

        // Debounce recreates: inspector drags change values every frame, and each structural change
        // is a full world rebuild. Wait for the config to sit still before applying it.
        constexpr int kRecreateDebounceFrames = 30;
        if (hash != m_pendingHash)
        {
            m_pendingHash = hash;
            m_pendingFrames = 0;
        }
        else if (m_pendingFrames < kRecreateDebounceFrames)
        {
            ++m_pendingFrames;
        }

        // Scene buffer rebuilds (scene load, play-stop restore) wipe the shared arena out from under a
        // live world — recreate instead of streaming into the wiped arena (warn spam, no terrain).
        const bool arenaDead = m_world && !m_world->IsArenaAlive();
        // Auto Rebuild off: worldgen edits stage silently and apply only on the button (forceRebuild).
        // First build (!m_world) and arena death still recreate unconditionally so the world exists.
        const bool configChanged =
            tag->autoRebuild && hash != m_appliedHash && m_pendingFrames >= kRecreateDebounceFrames;

        bool created = false;
        if (!m_world || arenaDead || forceRebuild || configChanged)
        {
            cfg.lod0Radius = lod0Radius;
            CreateWorld(scene, cfg);
            m_componentOwned = true;
            m_appliedHash = hash;
            m_appliedLod0Radius = lod0Radius;
            m_appliedPhysics = tag->physics;
            m_appliedFriction = tag->physicsFriction;
            m_appliedRestitution = tag->physicsRestitution;
            created = true;
        }
        // Live retunes (no rebuild): LOD band radius and the physics collider toggle are independent.
        if (!created && m_world && lod0Radius != m_appliedLod0Radius)
        {
            m_world->SetLod0Radius(lod0Radius);
            m_appliedLod0Radius = lod0Radius;
        }
        if (!created && m_world && tag->physics != m_appliedPhysics)
        {
            m_world->SetPhysicsEnabled(tag->physics);
            m_appliedPhysics = tag->physics;
        }
        if (!created && m_world &&
            (tag->physicsFriction != m_appliedFriction || tag->physicsRestitution != m_appliedRestitution))
        {
            m_world->SetTerrainMaterial(tag->physicsFriction, tag->physicsRestitution);
            m_appliedFriction = tag->physicsFriction;
            m_appliedRestitution = tag->physicsRestitution;
        }

        if (tag->anchorFollowsCamera)
        {
            if (Camera *camera = scene->GetActiveCamera())
                m_world->SetAnchor(camera->GetPosition());
        }
    }
} // namespace pe::voxel
