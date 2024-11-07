#pragma once

#if defined(PE_WIN32)
#define FUNC_EXPORT extern "C" __declspec(dllexport)
#else
#define FUNC_EXPORT extern "C"
#endif

// Macro to register a script class object creation function
// It is put in the source file of the script class if an object
// of this specific class needs to be created
#define REGISTER_SCRIPT(ClassName)                               \
    namespace                                                    \
    {                                                            \
        FUNC_EXPORT pe::ScriptObject *CreateObject_##ClassName() \
        {                                                        \
            return new ClassName();                              \
        }                                                        \
    }

namespace pe
{
    class ScriptObject
    {
    public:
        virtual void Init() {};
        virtual void Update() {};
        virtual void Draw() {};
        virtual void Destroy() {};
    };
}
