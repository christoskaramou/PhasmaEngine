#include "Vault.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>

#include <optional>
#include <stdexcept>

namespace phasma::driver
{
    namespace
    {
        using winrt::Windows::Security::Credentials::PasswordCredential;
        using winrt::Windows::Security::Credentials::PasswordVault;

        constexpr wchar_t resource[] = L"jev";
        constexpr wchar_t user[] = L"api-key";

        std::optional<PasswordCredential> Saved(const PasswordVault &vault)
        {
            try
            {
                return vault.Retrieve(resource, user);
            }
            catch (const winrt::hresult_error &)
            {
                return std::nullopt;
            }
        }
    } // namespace

    std::string ReadSavedJevKey()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            PasswordVault vault;
            auto credential = vault.Retrieve(resource, user);
            credential.RetrievePassword();

            return winrt::to_string(credential.Password());
        }
        catch (const winrt::hresult_error &)
        {
            throw std::runtime_error("No saved Jev key: Web Credential jev/api-key was not found or could not be read");
        }
    }

    bool HasSavedJevKey()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        return Saved(PasswordVault()).has_value();
    }

    // Replaces the saved key the Harness and skills also use. An overwriting Add leaves no moment without a key;
    // only if the vault keeps the old one is it swapped, with the old credential held to put back on failure.
    void SaveJevKey(const std::string &key)
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        PasswordVault vault;
        const auto password = winrt::to_hstring(key);
        try
        {
            vault.Add(PasswordCredential(resource, user, password));
            auto saved = vault.Retrieve(resource, user);
            saved.RetrievePassword();
            if (saved.Password() == password)
            {
                return;
            }
        }
        catch (const winrt::hresult_error &)
        {
        }

        auto old = Saved(vault);
        if (old)
        {
            old->RetrievePassword();
            vault.Remove(*old);
        }

        try
        {
            vault.Add(PasswordCredential(resource, user, password));
        }
        catch (const winrt::hresult_error &)
        {
            if (old)
            {
                vault.Add(*old);
            }
            throw std::runtime_error("The Jev key could not be saved");
        }
    }

    bool RemoveJevKey()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        PasswordVault vault;
        auto saved = Saved(vault);
        if (!saved)
        {
            return false;
        }

        vault.Remove(*saved);
        return true;
    }
} // namespace phasma::driver
