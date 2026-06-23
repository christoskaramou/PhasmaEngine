
namespace pe
{
    // Explicit instantiation — ensures a single Settings::Get<SceneSettings>()
    // definition exists in PhasmaCore.dll. Without this, each module (DLL + EXE)
    // gets its own function-local static 'value', splitting the settings singleton.
    template PE_API SceneSettings &Settings::Get<SceneSettings>();

    // Active post-process profile pointer. Lives in this one TU (PhasmaCore.dll) so every module
    // resolves the same instance, mirroring the Settings singleton. Null = use the scene default
    // (the SceneSettings singleton's PostProcessProfile base slice).
    static PostProcessProfile *s_activePostProcessProfile = nullptr;

    // Master switch driven by the Scene Settings node: true when an enabled node exists, false when
    // the node is disabled / deleted / absent. When false, the scene look (post-process + shadows)
    // is off; performance/debug toggles (culling, Forward+, grid) stay independent.
    static bool s_sceneSettingsActive = true;

    // The "everything off" profile returned while the Scene Settings node is inactive. Built once
    // with every effect toggle cleared (the SceneSettings values are left untouched so re-enabling
    // restores the previous set).
    const PostProcessProfile &DisabledPostProcessProfile()
    {
        static const PostProcessProfile disabled = []
        {
            PostProcessProfile p;
            p.ssao = p.fxaa = p.taa = p.cas_sharpening = p.ssr = p.tonemapping = p.color_grading = p.dof =
                p.bloom = p.motion_blur = p.IBL = false;
            return p;
        }();
        return disabled;
    }

    PostProcessProfile &ActivePostProcessProfile()
    {
        // An active PostProcessVolume wins over the master switch: the resolver already blended it over
        // the correct base (scene default when the Scene Settings node is active, else all-off), so a
        // volume turns its own effects on when entered even with no / disabled Scene Settings node.
        if (s_activePostProcessProfile)
            return *s_activePostProcessProfile;
        if (!s_sceneSettingsActive)
            return const_cast<PostProcessProfile &>(DisabledPostProcessProfile());
        return static_cast<PostProcessProfile &>(Settings::Get<SceneSettings>());
    }

    void SetActivePostProcessProfile(PostProcessProfile *profile)
    {
        s_activePostProcessProfile = profile;
    }

    // Per-effect blend factors for the current frame (pinned here like the profile so every module
    // agrees). Defaults to all-1 = running passes show full effect; the volume resolver overwrites it.
    static PostProcessBlend s_activePostProcessBlend{};

    PostProcessBlend &ActivePostProcessBlend()
    {
        return s_activePostProcessBlend;
    }

    void SetActivePostProcessBlend(const PostProcessBlend &blend)
    {
        s_activePostProcessBlend = blend;
    }

    bool SceneSettingsActive()
    {
        return s_sceneSettingsActive;
    }

    void SetSceneSettingsActive(bool active)
    {
        s_sceneSettingsActive = active;
    }
} // namespace pe
