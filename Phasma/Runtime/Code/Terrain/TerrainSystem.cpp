#include "Terrain/TerrainSystem.h"
#include "Camera/Camera.h" // anchor follows the active camera (streamed windows + collider ring)
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#ifdef PE_PHYSICS
#include "ECS/Context.h"           // GetGlobalSystem
#include "Systems/PhysicsSystem.h" // sim-state edge -> re-register the terrain collider
#endif

namespace pe::terrain
{
    namespace
    {
        uint64_t HashCombine(uint64_t h, uint64_t v)
        {
            return (h ^ v) * 1099511628211ull;
        }
        uint64_t HashInt(uint64_t h, int v)
        {
            return HashCombine(h, static_cast<uint64_t>(static_cast<int64_t>(v)));
        }
        uint64_t HashF(uint64_t h, float v)
        {
            return HashInt(h, static_cast<int>(v * 16.0f));
        }
        uint64_t HashString(uint64_t h, const std::string &s)
        {
            h = HashCombine(h, s.size());
            for (char c : s)
                h = HashCombine(h, static_cast<uint64_t>(static_cast<uint8_t>(c)));
            return h;
        }

        // Structural config: any change rebuilds the terrain (physics toggles retune live instead).
        uint64_t HashStructuralConfig(const TerrainConfig &cfg)
        {
            uint64_t h = 1469598103934665603ull;
            h = HashInt(h, cfg.sizeXMeters);
            h = HashInt(h, cfg.sizeZMeters);
            h = HashInt(h, cfg.boundsCenterCx);
            h = HashInt(h, cfg.boundsCenterCz);
            h = HashF(h, cfg.groundHeight);
            h = HashF(h, cfg.heightMin);
            h = HashF(h, cfg.heightMax);
            h = HashF(h, cfg.seaLevelM);
            h = HashString(h, cfg.heightmapPath);
            h = HashString(h, cfg.cavesPath);
            h = HashString(h, cfg.scatterPath);
            for (const std::string &s : cfg.scatterMeshes)
                h = HashString(h, s);
            h = HashString(h, cfg.splatPath);
            for (const std::string &s : cfg.layerPaths)
                h = HashString(h, s);
            for (const std::string &s : cfg.materialPaths)
                h = HashString(h, s);
            h = HashF(h, cfg.noiseFeatureScale);
            h = HashInt(h, cfg.noiseSeed);
            h = HashF(h, cfg.metersPerPixel);
            h = HashInt(h, cfg.streaming ? 1 : 0);
            h = HashF(h, cfg.overhangs);
            // collisionRadiusM is live-applied (ring re-fit), not structural.
            return h;
        }
    } // namespace

    TerrainSystem::~TerrainSystem()
    {
        Destroy();
    }

    void TerrainSystem::Init(CommandBuffer *)
    {
        SetEnabled(true);
    }

    void TerrainSystem::Update()
    {
        ReconcileComponentWorld();
        if (m_world)
        {
            // The anchor centres the streamed windows and the collider ring on the viewer.
            if (Scene *scene = GetActiveScene())
                if (Camera *camera = scene->GetActiveCamera())
                    m_world->SetAnchor(camera->GetPosition());
            m_world->Update();
        }
#ifdef PE_PHYSICS
        // Play start re-churns the host node, so re-register the collider on the sim false->true edge
        // (and drop it cleanly on stop). Re-applying the same flag re-cooks it.
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

    void TerrainSystem::Destroy()
    {
        if (m_world)
        {
            m_world->Destroy();
            m_world.reset();
        }
        m_componentOwned = false;
        m_appliedHash = 0;
        m_appliedPhysics = false;
        m_appliedFriction = 0.5f;
        m_appliedRestitution = 0.3f;
        // Stay enabled: the component reconcile must keep running to rebuild after play-stop teardown.
    }

    TerrainWorld *TerrainSystem::World()
    {
        return m_world.get();
    }

    TerrainWorld *TerrainSystem::CreateWorld(Scene *scene, const TerrainConfig &cfg, const std::vector<float> *ops)
    {
        if (!m_world)
            m_world = std::make_unique<TerrainWorld>();
        else
            m_world->Destroy();
        if (ops)
            m_world->SetOps(*ops); // before Create — tiles apply ops as they mesh
        m_world->Create(scene, cfg);
        m_componentOwned = false;
        m_appliedHash = 0;
        SetEnabled(true);
        return m_world.get();
    }

    void TerrainSystem::ReconcileComponentWorld()
    {
        Scene *scene = GetActiveScene();
        NodeId *node = scene ? scene->GetTerrainNode() : nullptr;
        NodeTerrainTag *tag = node ? scene->GetTerrainForNode(node) : nullptr;
        const bool want = tag && tag->worldEnabled && scene->IsNodeHierarchyEnabled(node);

        if (!want)
        {
            if (m_world && m_componentOwned)
            {
                m_world->Destroy();
                m_world.reset();
                m_componentOwned = false;
                m_appliedHash = 0;
            }
            return;
        }

        // A healthy script-created world owns the slot — leave it alone. A DEAD one (its host mesh wiped
        // by a scene rebuild) must not block the component forever — fall through and recreate.
        if (m_world && !m_componentOwned && m_world->IsAlive())
            return;

        TerrainConfig cfg{};
        cfg.sizeXMeters = tag->sizeXMeters;
        cfg.sizeZMeters = tag->sizeZMeters;
        cfg.groundHeight = tag->groundHeight;
        cfg.heightMin = tag->heightMin;
        cfg.heightMax = tag->heightMax;
        cfg.seaLevelM = tag->seaLevelM;
        cfg.heightmapPath = tag->heightmapPath;
        cfg.cavesPath = tag->cavesPath;
        cfg.scatterPath = tag->scatterPath;
        cfg.scatterMeshes = tag->scatterMeshes;
        cfg.splatPath = tag->splatPath;
        cfg.layerPaths = tag->layerPaths;
        cfg.materialPaths = tag->materialPaths;
        cfg.textureScaleM = tag->textureScaleM; // live (kept out of the structural hash below)
        cfg.noiseFeatureScale = tag->noiseFeatureScale;
        cfg.noiseSeed = tag->noiseSeed;
        cfg.metersPerPixel = tag->metersPerPixel;
        cfg.physics = tag->physics;
        cfg.physicsFriction = tag->physicsFriction;
        cfg.physicsRestitution = tag->physicsRestitution;
        cfg.streaming = tag->streaming;
        cfg.overhangs = tag->overhangs;
        cfg.collisionRadiusM = tag->collisionRadiusM;
        // The node's world position is the terrain centre.
        const vec3 p = vec3(scene->GetWorldMatrix(node)[3]);
        cfg.boundsCenterCx = static_cast<int>(std::floor(p.x / 16.0f));
        cfg.boundsCenterCz = static_cast<int>(std::floor(p.z / 16.0f));

        const uint64_t hash = HashStructuralConfig(cfg);

        // Inspector "Rebuild Terrain" button (and Map Painter Save): force an immediate rebuild — the only
        // way to pick up a repainted map behind an unchanged path, and the manual trigger with Auto off.
        bool forceRebuild = false;
        if (tag->rebuildRequested)
        {
            tag->rebuildRequested = false;
            forceRebuild = true;
        }

        // Debounce rebuilds: inspector drags change values every frame; wait for the config to settle.
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

        // Brush ops live on the world but persist through the tag (serialized with the scene, seeds
        // every recreate — so sculpts survive structural rebuilds too). Sync tag <- world only while
        // the world is ALIVE: a dead one (scene load wiped it) must not clobber ops just restored
        // from the file. Ops only append, so a count check suffices.
        if (m_world && m_componentOwned && m_world->IsAlive() &&
            m_world->OpCount() * 7 != tag->terrainOps.size())
            m_world->GetOps(tag->terrainOps);

        const bool aliveDead = m_world && !m_world->IsAlive();
        const bool configChanged =
            tag->autoRebuild && hash != m_appliedHash && m_pendingFrames >= kRecreateDebounceFrames;

        bool created = false;
        if (!m_world || aliveDead || forceRebuild || configChanged)
        {
            CreateWorld(scene, cfg, &tag->terrainOps);
            m_componentOwned = true;
            m_appliedHash = hash;
            m_appliedPhysics = tag->physics;
            m_appliedFriction = tag->physicsFriction;
            m_appliedRestitution = tag->physicsRestitution;
            created = true;
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
        if (!created && m_world && tag->collisionRadiusM != m_world->Config().collisionRadiusM)
            m_world->SetCollisionRadius(tag->collisionRadiusM);
        // Texture scale is live (not in the structural hash): push it every frame it differs so the
        // inspector slider retextures with no rebuild.
        if (m_world && tag->textureScaleM != m_world->Config().textureScaleM)
            m_world->SetTextureScale(tag->textureScaleM);
    }
} // namespace pe::terrain
