#include "NativeScriptGuard.h"
#if defined(_MSC_VER)
#include <excpt.h>
#include <malloc.h>
#endif

// No engine headers and no objects with destructors in the __try frames.
namespace pe
{
#if defined(_MSC_VER)
    namespace
    {
        int Filter(unsigned long code, uint32_t *fault)
        {
            // C++ exceptions keep their normal semantics (the noexcept trampolines handle them).
            if (code == 0xE06D7363u)
                return EXCEPTION_CONTINUE_SEARCH;
            *fault = code ? static_cast<uint32_t>(code) : 1u;
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void Recover(uint32_t fault)
        {
            if (fault == 0xC00000FDu) // stack overflow: restore the guard page
                _resetstkoflw();
        }
    } // namespace

    bool NativeFaultsContained()
    {
        return true;
    }

    uint32_t GuardedCreate(decltype(phasma::ScriptDesc::create) create, const phasma::ScriptApi *api, phasma::Node node, void **state, uint32_t *fault)
    {
        *fault = 0;
        uint32_t result = 0;
        __try
        {
            result = create(api, node, state);
        }
        __except (Filter(GetExceptionCode(), fault))
        {
            result = 0;
        }
        Recover(*fault);
        return result;
    }

    uint32_t GuardedUpdate(decltype(phasma::ScriptDesc::update) update, void *state, double dt, uint32_t *fault)
    {
        *fault = 0;
        uint32_t result = 0;
        __try
        {
            result = update(state, dt);
        }
        __except (Filter(GetExceptionCode(), fault))
        {
            result = 0;
        }
        Recover(*fault);
        return result;
    }

    void GuardedDestroy(decltype(phasma::ScriptDesc::destroy) destroy, void *state, uint32_t *fault)
    {
        *fault = 0;
        __try
        {
            destroy(state);
        }
        __except (Filter(GetExceptionCode(), fault))
        {
        }
        Recover(*fault);
    }
#else
    bool NativeFaultsContained()
    {
        return false;
    }

    uint32_t GuardedCreate(decltype(phasma::ScriptDesc::create) create, const phasma::ScriptApi *api, phasma::Node node, void **state, uint32_t *fault)
    {
        *fault = 0;
        return create(api, node, state);
    }

    uint32_t GuardedUpdate(decltype(phasma::ScriptDesc::update) update, void *state, double dt, uint32_t *fault)
    {
        *fault = 0;
        return update(state, dt);
    }

    void GuardedDestroy(decltype(phasma::ScriptDesc::destroy) destroy, void *state, uint32_t *fault)
    {
        *fault = 0;
        destroy(state);
    }
#endif
} // namespace pe
