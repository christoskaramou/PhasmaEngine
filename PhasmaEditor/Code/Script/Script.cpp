#include "Script.h"
#include "ScriptManager.h"
#include "ScriptObject.h"

namespace pe
{
    void Script::Init()
    {
        if (m_scriptObject)
            m_scriptObject->Init();
    }

    void Script::Update()
    {
        if (m_scriptObject)
            m_scriptObject->Update();
    }

    void Script::Draw()
    {
        if (m_scriptObject)
            m_scriptObject->Draw();
    }

    void Script::Destroy()
    {
        if (m_scriptObject)
            m_scriptObject->Destroy();
    }

    void Script::CreateObject()
    {
        // Get the class name from the path
        size_t start = m_path.find_last_of('/') + 1;
        size_t end = m_path.find_last_of('.');
        std::string className = m_path.substr(start, end - start);

        // Load the factory function
        m_createObjectFunc = nullptr;
        std::string factoryFuncName = "CreateObject_" + className;
#if defined(PE_WIN32)
        m_createObjectFunc = (CreateObjectFunc)GetProcAddress((HMODULE)ScriptManager::GetModule(), factoryFuncName.c_str());
#else
        m_createObjectFunc = (CreateObjectFunc)dlsym(ScriptManager::GetModule(), factoryFuncName.c_str());
#endif
        PE_ERROR_IF(!m_createObjectFunc, "Failed to load CreateScript function");
        m_scriptObject = m_createObjectFunc();
    }
} // namespace pe
