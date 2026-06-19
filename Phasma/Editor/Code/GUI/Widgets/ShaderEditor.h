// Phasma/Editor/Code/GUI/Widgets/ShaderEditor.h
#pragma once
#include "GUI/Widget.h"
#include "TextEditor.h"

namespace pe
{
    class ShaderEditor : public Widget
    {
    public:
        ShaderEditor() : Widget("Shader Editor") { m_open = false; }
        void Update() override;

    private:
        void ScanShaderFiles();
        void LoadShaderFile(const std::string &path);
        void SaveAndRecompile();

        std::vector<std::string> m_shaderFiles;
        std::vector<std::string> m_shaderRelPaths;
        int m_selectedShader = -1;
        std::string m_shaderOriginalSource;
        TextEditor m_editor;
        bool m_shaderModified = false;
        char m_shaderSearchFilter[128] = {};
        int m_editorPalette = 4;
        int m_editorFontSizeIdx = 1;
        float m_editorFontScale = 1.0f;
        bool m_shaderFilesScanned = false;
        bool m_paletteInitialized = false; // member, not function-local static
    };
} // namespace pe
