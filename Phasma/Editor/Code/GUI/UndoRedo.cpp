#include "UndoRedo.h"
#include "Scene/Scene.h"
#include "rapidjson/document.h"

namespace pe
{
    // ---------------------------------------------------------------------------
    // DiffSnapshots — compare two JSON snapshots and return a one-line label
    // ---------------------------------------------------------------------------
    static std::string DiffSnapshots(const std::string &before, const std::string &after)
    {
        rapidjson::Document bDoc, aDoc;
        bDoc.Parse(before.c_str(), before.size());
        aDoc.Parse(after.c_str(), after.size());
        if (bDoc.HasParseError() || aDoc.HasParseError())
            return "Scene Change";

        // 1. Sources (models loaded/removed) — check first, highest priority
        int bSrc = bDoc.HasMember("sources") ? static_cast<int>(bDoc["sources"].Size()) : 0;
        int aSrc = aDoc.HasMember("sources") ? static_cast<int>(aDoc["sources"].Size()) : 0;
        if (aSrc > bSrc)
            return "Loaded Model";
        if (aSrc < bSrc)
            return "Removed Model";

        // 2. Node count
        int bNodeCount = bDoc.HasMember("nodes") ? static_cast<int>(bDoc["nodes"].Size()) : 0;
        int aNodeCount = aDoc.HasMember("nodes") ? static_cast<int>(aDoc["nodes"].Size()) : 0;
        if (aNodeCount > bNodeCount)
            return "Added Node";
        if (aNodeCount < bNodeCount)
        {
            // Find deleted node name from before-snapshot
            if (bDoc.HasMember("nodes") && aDoc.HasMember("nodes"))
            {
                const auto &bNodes = bDoc["nodes"];
                const auto &aNodes = aDoc["nodes"];
                std::unordered_set<std::string> afterNames;
                for (rapidjson::SizeType i = 0; i < aNodes.Size(); i++)
                    if (aNodes[i].HasMember("name"))
                        afterNames.insert(aNodes[i]["name"].GetString());
                for (rapidjson::SizeType i = 0; i < bNodes.Size(); i++)
                    if (bNodes[i].HasMember("name") &&
                        afterNames.find(bNodes[i]["name"].GetString()) == afterNames.end())
                        return "Deleted " + std::string(bNodes[i]["name"].GetString());
            }
            return "Deleted Node";
        }

        // 3. Light count
        int bLights = bDoc.HasMember("lights") ? static_cast<int>(bDoc["lights"].Size()) : 0;
        int aLights = aDoc.HasMember("lights") ? static_cast<int>(aDoc["lights"].Size()) : 0;
        if (aLights > bLights)
            return "Added Light";
        if (aLights < bLights)
            return "Removed Light";

        // 4. Camera count
        int bCams = bDoc.HasMember("cameras") ? static_cast<int>(bDoc["cameras"].Size()) : 0;
        int aCams = aDoc.HasMember("cameras") ? static_cast<int>(aDoc["cameras"].Size()) : 0;
        if (aCams > bCams)
            return "Added Camera";
        if (aCams < bCams)
            return "Removed Camera";

        // 5. Per-node diffs (same node count)
        if (bDoc.HasMember("nodes") && aDoc.HasMember("nodes"))
        {
            const auto &bNodes = bDoc["nodes"];
            const auto &aNodes = aDoc["nodes"];
            rapidjson::SizeType count =
                std::min(bNodes.Size(), aNodes.Size());

            for (rapidjson::SizeType i = 0; i < count; i++)
            {
                const auto &bn = bNodes[i];
                const auto &an = aNodes[i];
                std::string nodeName =
                    bn.HasMember("name") ? bn["name"].GetString()
                                         : ("Node " + std::to_string(i));

                // mesh_refs (handle both "mesh" and "mesh_refs" keys)
                auto getMeshCount = [](const rapidjson::Value &n) -> int
                {
                    if (n.HasMember("mesh_refs"))
                        return static_cast<int>(n["mesh_refs"].Size());
                    if (n.HasMember("mesh") && n["mesh"].GetInt() >= 0)
                        return 1;
                    return 0;
                };
                int bMR = getMeshCount(bn);
                int aMR = getMeshCount(an);
                if (aMR > bMR)
                    return "Assigned Mesh on " + nodeName;
                if (aMR < bMR)
                    return "Removed Mesh on " + nodeName;

                // local_matrix transform changes
                if (bn.HasMember("local_matrix") && an.HasMember("local_matrix"))
                {
                    const auto &bm = bn["local_matrix"];
                    const auto &am = an["local_matrix"];
                    if (!bm.IsArray() || !am.IsArray() || bm.Size() < 16 || am.Size() < 16)
                        continue;
                    constexpr float eps = 1e-4f;

                    // Translation: column 3 xyz = indices 12, 13, 14
                    bool transChanged = false;
                    for (int j = 12; j <= 14; j++)
                        if (std::abs(bm[j].GetFloat() - am[j].GetFloat()) > eps)
                        {
                            transChanged = true;
                            break;
                        }
                    if (transChanged)
                        return "Moved " + nodeName;

                    // Scale: lengths of columns 0, 1, 2 (xyz of each, base indices 0, 4, 8)
                    bool scaleChanged = false;
                    for (int c = 0; c < 3; c++)
                    {
                        int b = c * 4;
                        auto len = [&](const rapidjson::Value &m) -> float
                        {
                            float x = m[b].GetFloat(), y = m[b + 1].GetFloat(),
                                  z = m[b + 2].GetFloat();
                            return std::sqrt(x * x + y * y + z * z);
                        };
                        if (std::abs(len(bm) - len(am)) > eps)
                        {
                            scaleChanged = true;
                            break;
                        }
                    }
                    if (scaleChanged)
                        return "Scaled " + nodeName;

                    // Rotation: any change in columns 0–2 (indices 0–11)
                    for (int j = 0; j < 12; j++)
                        if (std::abs(bm[j].GetFloat() - am[j].GetFloat()) > eps)
                            return "Rotated " + nodeName;
                }
            }
        }

        // 6. Material changes — compare mesh material_factors
        if (bDoc.HasMember("meshes") && aDoc.HasMember("meshes") &&
            bDoc.HasMember("nodes") && aDoc.HasMember("nodes"))
        {
            const auto &bMeshes = bDoc["meshes"];
            const auto &aMeshes = aDoc["meshes"];
            const auto &bNodes = bDoc["nodes"];
            rapidjson::SizeType mc = std::min(bMeshes.Size(), aMeshes.Size());
            for (rapidjson::SizeType mi = 0; mi < mc; mi++)
            {
                const auto &bm = bMeshes[mi];
                const auto &am = aMeshes[mi];
                bool matChanged = false;
                if (bm.HasMember("material_factors") && am.HasMember("material_factors"))
                {
                    const auto &bf = bm["material_factors"];
                    const auto &af = am["material_factors"];
                    constexpr float eps = 1e-4f;
                    for (rapidjson::SizeType fi = 0; fi < std::min(bf.Size(), af.Size()) && !matChanged; fi++)
                    {
                        const auto &bfArr = bf[fi];
                        const auto &afArr = af[fi];
                        for (rapidjson::SizeType k = 0; k < std::min(bfArr.Size(), afArr.Size()) && !matChanged; k++)
                            if (std::abs(bfArr[k].GetFloat() - afArr[k].GetFloat()) > eps)
                                matChanged = true;
                    }
                }
                if (!matChanged && bm.HasMember("render_type") && am.HasMember("render_type"))
                    matChanged = (bm["render_type"].GetInt() != am["render_type"].GetInt());

                if (matChanged)
                {
                    // Find the node that references mesh index mi
                    for (rapidjson::SizeType ni = 0; ni < bNodes.Size(); ni++)
                    {
                        const auto &bn = bNodes[ni];
                        bool refs = false;
                        if (bn.HasMember("mesh_refs"))
                        {
                            for (rapidjson::SizeType r = 0; r < bn["mesh_refs"].Size(); r++)
                            {
                                int idx = bn["mesh_refs"][r].GetInt();
                                if (idx >= 0 && static_cast<rapidjson::SizeType>(idx) == mi)
                                {
                                    refs = true;
                                    break;
                                }
                            }
                        }
                        else if (bn.HasMember("mesh"))
                        {
                            int idx = bn["mesh"].GetInt();
                            if (idx >= 0 && static_cast<rapidjson::SizeType>(idx) == mi)
                                refs = true;
                        }
                        if (refs && bn.HasMember("name"))
                            return "Changed Material on " + std::string(bn["name"].GetString());
                    }
                    return "Changed Material";
                }
            }
        }

        return "Scene Change";
    }

    // ---------------------------------------------------------------------------
    // UndoRedo methods
    // ---------------------------------------------------------------------------

    UndoRedo &UndoRedo::Instance()
    {
        static UndoRedo instance;
        return instance;
    }

    void UndoRedo::CaptureIdleState(Scene &scene)
    {
        if (m_restoring)
            return;

        std::string current = scene.TakeSnapshot();

        if (m_hasIdleSnapshot && m_settleFrames == 0 && current != m_idleSnapshot)
        {
            std::string label = DiffSnapshots(m_idleSnapshot, current);
            PushUndo({std::move(m_idleSnapshot), std::move(label)});
            m_redoStack.clear();
            scene.MarkDirty();
        }

        if (m_settleFrames > 0)
            --m_settleFrames;

        m_idleSnapshot = std::move(current);
        m_hasIdleSnapshot = true;
    }

    void UndoRedo::RecordSnapshot(Scene &scene, std::string label)
    {
        if (m_restoring)
            return;

        std::string current = scene.TakeSnapshot();
        PushUndo({std::move(current), std::move(label)});
        m_redoStack.clear();
        scene.MarkDirty();

        m_hasIdleSnapshot = false;
    }

    void UndoRedo::Undo(Scene &scene)
    {
        if (m_undoStack.empty())
            return;

        HistoryEntry entry = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        HistoryEntry current{scene.TakeSnapshot(), entry.label};

        if (RestoreEntry(scene, entry))
            PushRedo(std::move(current));
        else
            PushUndo(std::move(entry));
    }

    void UndoRedo::Redo(Scene &scene)
    {
        if (m_redoStack.empty())
            return;

        HistoryEntry entry = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        HistoryEntry current{scene.TakeSnapshot(), entry.label};

        if (RestoreEntry(scene, entry))
            PushUndo(std::move(current));
        else
            PushRedo(std::move(entry));
    }

    void UndoRedo::UndoTo(Scene &scene, size_t stepsBack)
    {
        if (stepsBack == 0 || m_undoStack.empty())
            return;

        stepsBack = std::min(stepsBack, m_undoStack.size());

        std::vector<HistoryEntry> popped;
        popped.reserve(stepsBack);
        for (size_t i = 0; i < stepsBack; i++)
        {
            popped.push_back(std::move(m_undoStack.back()));
            m_undoStack.pop_back();
        }

        HistoryEntry current{scene.TakeSnapshot(), "Scene Change"};
        HistoryEntry target = std::move(popped.back());
        popped.pop_back();

        if (RestoreEntry(scene, target))
        {
            PushRedo(std::move(current));
            for (auto &entry : popped)
                PushRedo(std::move(entry));
        }
        else
        {
            m_undoStack.push_back(std::move(target));
            for (auto it = popped.rbegin(); it != popped.rend(); ++it)
                m_undoStack.push_back(std::move(*it));
        }
    }

    void UndoRedo::RedoTo(Scene &scene, size_t stepsForward)
    {
        if (stepsForward == 0 || m_redoStack.empty())
            return;

        stepsForward = std::min(stepsForward, m_redoStack.size());

        std::vector<HistoryEntry> popped;
        popped.reserve(stepsForward);
        for (size_t i = 0; i < stepsForward; i++)
        {
            popped.push_back(std::move(m_redoStack.back()));
            m_redoStack.pop_back();
        }

        HistoryEntry current{scene.TakeSnapshot(), "Scene Change"};
        HistoryEntry target = std::move(popped.back());
        popped.pop_back();

        if (RestoreEntry(scene, target))
        {
            PushUndo(std::move(current));
            for (auto &entry : popped)
                PushUndo(std::move(entry));
        }
        else
        {
            m_redoStack.push_back(std::move(target));
            for (auto it = popped.rbegin(); it != popped.rend(); ++it)
                m_redoStack.push_back(std::move(*it));
        }
    }

    void UndoRedo::Clear()
    {
        m_undoStack.clear();
        m_redoStack.clear();
        m_idleSnapshot.clear();
        m_hasIdleSnapshot = false;
        m_restoring = false;
        m_settleFrames = 0;
    }

    void UndoRedo::PushUndo(HistoryEntry entry)
    {
        if (entry.snapshot.empty())
            return;
        if (!m_undoStack.empty() && m_undoStack.back().snapshot == entry.snapshot)
            return;

        m_undoStack.push_back(std::move(entry));
        if (m_undoStack.size() > MAX_HISTORY)
            m_undoStack.pop_front();
    }

    void UndoRedo::PushRedo(HistoryEntry entry)
    {
        if (entry.snapshot.empty())
            return;
        if (!m_redoStack.empty() && m_redoStack.back().snapshot == entry.snapshot)
            return;

        m_redoStack.push_back(std::move(entry));
        if (m_redoStack.size() > MAX_HISTORY)
            m_redoStack.pop_front();
    }

    bool UndoRedo::RestoreEntry(Scene &scene, const HistoryEntry &entry)
    {
        if (entry.snapshot.empty())
            return false;

        m_restoring = true;
        const bool restored = scene.RestoreSnapshot(entry.snapshot);
        m_restoring = false;

        if (!restored)
            return false;

        scene.MarkDirty();
        m_idleSnapshot.clear();
        m_hasIdleSnapshot = false;
        m_settleFrames = SETTLE_FRAMES;
        return true;
    }
} // namespace pe
