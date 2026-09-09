// SPDX-License-Identifier: GPL-2.0-or-later

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

class DllDirectory final {
public:
    explicit DllDirectory(const std::filesystem::path &path) noexcept
    {
        if (!path.empty()) {
            cookie_ = AddDllDirectory(path.c_str());
        }
    }

    ~DllDirectory()
    {
        if (cookie_ != nullptr) {
            RemoveDllDirectory(cookie_);
        }
    }

    DllDirectory(const DllDirectory &) = delete;
    DllDirectory &operator=(const DllDirectory &) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return cookie_ != nullptr;
    }

private:
    DLL_DIRECTORY_COOKIE cookie_ = nullptr;
};

std::wstring windows_error(DWORD error)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0U,
        reinterpret_cast<LPWSTR>(&buffer),
        0U,
        nullptr);

    std::wstring message;
    if (length != 0U && buffer != nullptr) {
        message.assign(buffer, length);
        LocalFree(buffer);
    }
    return message;
}

int fail(const std::wstring &message)
{
    std::wcerr << message << L'\n';
    return 1;
}

} // namespace

int wmain(int argument_count, wchar_t **arguments)
{
    if (argument_count < 4) {
        return fail(
            L"Expected the plugin path, OBS runtime directory, and frontend API directory");
    }

    const std::filesystem::path plugin_path =
        std::filesystem::absolute(arguments[1]);
    if (!std::filesystem::is_regular_file(plugin_path)) {
        return fail(L"The ChatView plugin DLL does not exist");
    }

    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        return fail(L"SetDefaultDllDirectories failed");
    }

    std::vector<std::filesystem::path> directories;
    directories.push_back(plugin_path.parent_path());
    for (int index = 2; index < argument_count; ++index) {
        const std::filesystem::path directory =
            std::filesystem::absolute(arguments[index]);
        if (!std::filesystem::is_directory(directory)) {
            return fail(L"A required DLL directory does not exist: " + directory.wstring());
        }
        directories.push_back(directory);
    }

    std::vector<std::unique_ptr<DllDirectory>> cookies;
    for (const std::filesystem::path &directory : directories) {
        auto cookie = std::make_unique<DllDirectory>(directory);
        if (!cookie->valid()) {
            return fail(L"AddDllDirectory failed for: " + directory.wstring());
        }
        cookies.push_back(std::move(cookie));
    }

    HMODULE module = LoadLibraryExW(
        plugin_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
            LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (module == nullptr) {
        const DWORD error = GetLastError();
        return fail(
            L"LoadLibraryExW failed with error " + std::to_wstring(error) + L": " +
            windows_error(error));
    }

    constexpr const char *required_exports[] = {
        "obs_module_description",
        "obs_module_get_string",
        "obs_module_load",
        "obs_module_name",
        "obs_module_set_pointer",
        "obs_module_unload",
        "obs_module_ver",
    };
    for (const char *name : required_exports) {
        if (GetProcAddress(module, name) == nullptr) {
            FreeLibrary(module);
            return fail(L"A required OBS module export is missing");
        }
    }

    if (!FreeLibrary(module)) {
        return fail(L"FreeLibrary failed for the ChatView plugin");
    }
    return 0;
}
