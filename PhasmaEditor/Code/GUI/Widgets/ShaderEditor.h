// PhasmaEditor/Code/GUI/Widgets/ShaderEditor.h
#pragma once
#include "GUI/Widget.h"
#include "TextEditor.h"
#include <vector>
#include <string>

namespace pe
{
    class AICompletionService;

    class ShaderEditor : public Widget
    {
    public:
        ShaderEditor() : Widget("Shader Editor") { m_open = false; }
        void Update() override;
        void SetCompletionService(AICompletionService *svc) { m_completion = svc; }

    private:
        void ScanShaderFiles();
        void LoadShaderFile(const std::string &path);
        void SaveAndRecompile();

        AICompletionService *m_completion = nullptr;

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

        // Ghost text
        std::string m_ghostText;
        bool m_completionPending = false;
        TextEditor::Coordinates m_savedCursor; // cursor position when Ctrl+Space was pressed
    };
} // namespace pe
