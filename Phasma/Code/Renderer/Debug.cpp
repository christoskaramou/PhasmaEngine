#include "Renderer/Debug.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "RenderDoc/renderdoc_app.h"

namespace pe
{
#if PE_DEBUG_MODE
    // Frame Capture
    HMODULE capture_module = nullptr;
    RENDERDOC_API_1_5_0 *capture_api = nullptr;

#if defined(WIN32) && PE_RENDER_DOC == 1
    std::wstring TryGetRenderDocPath()
    {
        // Query registry for all the render doc paths
        static const wchar_t *pszInstallerFolders = TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders");

        HKEY hkey;
        LSTATUS status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, pszInstallerFolders, 0, KEY_READ, &hkey);
        if (status != ERROR_SUCCESS) // ensure installer folders key is successfully opened
            return std::wstring();

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 8192
        TCHAR achClass[MAX_PATH] = TEXT(""); // buffer for class name
        DWORD cchClassName = MAX_PATH;       // size of class string
        DWORD cSubKeys = 0;                  // number of subkeys
        DWORD cbMaxSubKey;                   // longest subkey size
        DWORD cchMaxClass;                   // longest class string
        DWORD cValues;                       // number of values for keyPath
        DWORD cchMaxValue;                   // longest value name
        DWORD cbMaxValueData;                // longest value data
        DWORD cbSecurityDescriptor;          // size of security descriptor
        FILETIME ftLastWriteTime;            // last write time

        wchar_t cbEnumValue[MAX_VALUE_NAME] = TEXT("");

        DWORD i, retCode;

        TCHAR achValue[MAX_VALUE_NAME];
        DWORD cchValue = MAX_VALUE_NAME;

        // Get the class name and the value count.
        retCode = RegQueryInfoKey(
            hkey,                  // keyPath handle
            achClass,              // buffer for class name
            &cchClassName,         // size of class string
            nullptr,               // reserved
            &cSubKeys,             // number of subkeys
            &cbMaxSubKey,          // longest subkey size
            &cchMaxClass,          // longest class string
            &cValues,              // number of values for this keyPath
            &cchMaxValue,          // longest value name
            &cbMaxValueData,       // longest value data
            &cbSecurityDescriptor, // security descriptor
            &ftLastWriteTime);     // last write time

        if (cValues)
        {
            // printf("\nNumber of values: %d\n", cValues);
            for (i = 0, retCode = ERROR_SUCCESS; i < cValues; i++)
            {
                cchValue = MAX_VALUE_NAME;
                achValue[0] = '\0';
                DWORD type = REG_SZ;
                DWORD size;
                memset(cbEnumValue, '\0', MAX_VALUE_NAME);

                // MSDN:  https://docs.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea
                // If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, the string may not have been stored with
                // the proper null-terminating characters. Therefore, even if the function returns ERROR_SUCCESS, the application
                // should ensure that the string is properly terminated before using it; otherwise, it may overwrite a buffer.
                retCode = RegEnumValue(hkey, i,
                                       achValue,
                                       &cchValue,
                                       nullptr,
                                       &type,
                                       nullptr,
                                       &size);

                if (type != REG_SZ || retCode != ERROR_SUCCESS)
                    continue;

                retCode = RegQueryInfoKey(
                    hkey,                  // keyPath handle
                    achClass,              // buffer for class name
                    &cchClassName,         // size of class string
                    nullptr,               // reserved
                    &cSubKeys,             // number of subkeys
                    &cbMaxSubKey,          // longest subkey size
                    &cchMaxClass,          // longest class string
                    &cValues,              // number of values for this keyPath
                    &cchMaxValue,          // longest value name
                    &cbMaxValueData,       // longest value data
                    &cbSecurityDescriptor, // security descriptor
                    &ftLastWriteTime);     // last write time

                std::wstring path(achValue);
                if (path.find(L"RenderDoc") != std::wstring::npos)
                {
                    // many paths qualify:
                    //
                    // "C:\\Program Files\\RenderDoc\\plugins\\amd\\counters\\"
                    // "C:\\Program Files\\RenderDoc\\"
                    // "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\RenderDoc\\"
                    //
                    // Only consider the ones the contain the dll we want
                    const std::wstring rdc_dll_path = path += TEXT("renderdoc.dll");
                    WIN32_FIND_DATA find_file_data = {0};
                    HANDLE file_handle = FindFirstFile(rdc_dll_path.c_str(), &find_file_data);

                    if (file_handle != INVALID_HANDLE_VALUE)
                    {
                        RegCloseKey(hkey);
                        return path;
                    }
                }
            }
        }

        return std::wstring();
    }
#endif

    void Debug::InitCaptureApi()
    {
#if PE_RENDER_DOC == 1
        std::wstring renderdoc_path = TryGetRenderDocPath();
        if (renderdoc_path.empty())
            renderdoc_path = TEXT("renderdoc.dll"); // try to get it either way, maybe as an evniroment variable

        capture_module = ::GetModuleHandleW(renderdoc_path.c_str());
        if (!capture_module)
            capture_module = ::LoadLibraryW(renderdoc_path.c_str());

        if (capture_module)
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)::GetProcAddress(capture_module, "RENDERDOC_GetAPI");
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&capture_api);
            assert(ret == 1);

            capture_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
            capture_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
        }
#endif
    }

    void Debug::DestroyCaptureApi()
    {
        if (capture_module != nullptr)
            ::FreeLibrary(capture_module);
    }

    void Debug::StartFrameCapture()
    {
        if (capture_api)
            capture_api->StartFrameCapture(NULL, NULL);
    }

    void Debug::EndFrameCapture()
    {
        if (capture_api)
            capture_api->EndFrameCapture(NULL, NULL);
    }

    void Debug::TriggerCapture()
    {
        if (capture_api)
        {
            capture_api->TriggerCapture();
            if (capture_api->IsTargetControlConnected())
                capture_api->ShowReplayUI();
            else
                capture_api->LaunchReplayUI(true, "");
        }
    }
#endif
};
