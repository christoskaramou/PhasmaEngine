#include "Voxel/VoxelSystem.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe::voxel
{
    namespace
    {
        uint64_t HashCombine(uint64_t h, uint64_t v)
        {
            return (h ^ v) * 1099511628211ull; // FNV-1a step
        }

        // Structural config: any change here recreates the world (lod0Radius retunes live instead).
        uint64_t HashStructuralConfig(const VoxelConfig &cfg)
        {
            uint64_t h = 1469598103934665603ull;
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.loadRadius)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.unloadMargin)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.uploadBudgetPerFrame)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.groundY)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.worldRadius)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.boundsCenterCx)));
            h = HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(cfg.boundsCenterCz)));
            h = HashCombine(h, cfg.streaming ? 1u : 0u);
            for (char c : cfg.saveDir)
                h = HashCombine(h, static_cast<uint64_t>(static_cast<uint8_t>(c)));
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

        if (!m_world || arenaDead || (hash != m_appliedHash && m_pendingFrames >= kRecreateDebounceFrames))
        {
            cfg.lod0Radius = lod0Radius;
            CreateWorld(scene, cfg);
            m_componentOwned = true;
            m_appliedHash = hash;
            m_appliedLod0Radius = lod0Radius;
        }
        else if (lod0Radius != m_appliedLod0Radius)
        {
            m_world->SetLod0Radius(lod0Radius);
            m_appliedLod0Radius = lod0Radius;
        }

        if (tag->anchorFollowsCamera)
        {
            if (Camera *camera = scene->GetActiveCamera())
                m_world->SetAnchor(camera->GetPosition());
        }
    }
} // namespace pe::voxel
