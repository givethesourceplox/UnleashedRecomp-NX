#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nfd.h>

namespace
{
    constexpr const char* kSwitchInstallDropPath = "sdmc:/switch/UnleashedRecomp/install";
    constexpr const char* kGameExecutableFile = "default.xex";
    constexpr const char* kUpdateExecutablePatchFile = "default.xexp";
    constexpr const char* kDLCValidationFile = "DLC.xml";

    struct SwitchPathSet
    {
        std::vector<std::string> paths;
    };

    struct SwitchPathSetEnumerator
    {
        const SwitchPathSet* pathSet = nullptr;
        size_t index = 0;
    };

    std::string g_lastError;

    void SetError(std::string error)
    {
        g_lastError = std::move(error);
    }

    void ClearOutputPath(nfdnchar_t** outPath)
    {
        if (outPath != nullptr)
        {
            *outPath = nullptr;
        }
    }

    void ClearOutputPathSet(const nfdpathset_t** outPaths)
    {
        if (outPaths != nullptr)
        {
            *outPaths = nullptr;
        }
    }

    nfdnchar_t* CopyPath(const std::string& path)
    {
        auto* copy = static_cast<nfdnchar_t*>(std::malloc(path.size() + 1));
        if (copy == nullptr)
        {
            SetError("Out of memory while preparing Switch install paths.");
            return nullptr;
        }

        std::memcpy(copy, path.c_str(), path.size() + 1);
        return copy;
    }

    bool DirectoryContainsContentMarker(const std::filesystem::path& directory)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
        {
            return false;
        }

        return std::filesystem::exists(directory / kGameExecutableFile, ec) ||
            std::filesystem::exists(directory / kUpdateExecutablePatchFile, ec) ||
            std::filesystem::exists(directory / kDLCValidationFile, ec);
    }

    bool DirectoryContainsNestedContent(const std::filesystem::path& directory)
    {
        if (DirectoryContainsContentMarker(directory))
        {
            return true;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec)
            {
                return false;
            }

            if (!entry.is_directory(ec))
            {
                continue;
            }

            const std::filesystem::path entryPath = entry.path();
            if (DirectoryContainsContentMarker(entryPath))
            {
                return true;
            }

            for (const auto& childEntry : std::filesystem::directory_iterator(entryPath, ec))
            {
                if (ec)
                {
                    break;
                }

                if (childEntry.is_directory(ec) && DirectoryContainsContentMarker(childEntry.path()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    nfdresult_t SetSinglePath(nfdnchar_t** outPath, const std::string& path)
    {
        ClearOutputPath(outPath);

        if (outPath == nullptr)
        {
            SetError("Invalid output path.");
            return NFD_ERROR;
        }

        *outPath = CopyPath(path);
        return (*outPath != nullptr) ? NFD_OKAY : NFD_ERROR;
    }

    nfdresult_t CollectInstallPaths(const nfdpathset_t** outPaths, bool includeFiles, bool includeDirectories)
    {
        ClearOutputPathSet(outPaths);

        if (outPaths == nullptr)
        {
            SetError("Invalid output path set.");
            return NFD_ERROR;
        }

        std::error_code ec;
        const std::filesystem::path installDropPath(kSwitchInstallDropPath);
        if (!std::filesystem::exists(installDropPath, ec) || !std::filesystem::is_directory(installDropPath, ec))
        {
            SetError(std::string("Create ") + kSwitchInstallDropPath + " and put your Xbox 360 ISO, GOD/STFS containers, update, or DLC files there.");
            return NFD_ERROR;
        }

        auto pathSet = std::make_unique<SwitchPathSet>();
        for (const auto& entry : std::filesystem::directory_iterator(installDropPath, ec))
        {
            if (ec)
            {
                SetError("Failed to read the Switch install drop folder.");
                return NFD_ERROR;
            }

            const bool isFile = entry.is_regular_file(ec);
            const bool isDirectory = entry.is_directory(ec);
            if ((includeFiles && isFile) || (includeDirectories && isDirectory && DirectoryContainsNestedContent(entry.path())))
            {
                std::u8string path = entry.path().u8string();
                pathSet->paths.emplace_back(reinterpret_cast<const char*>(path.c_str()));
            }
        }

        std::sort(pathSet->paths.begin(), pathSet->paths.end());

        if (pathSet->paths.empty())
        {
            SetError(std::string("No selectable content was found in ") + kSwitchInstallDropPath + ". For extracted folders, put default.xex or default.xexp at the folder root or one nested folder below it.");
            return NFD_ERROR;
        }

        *outPaths = reinterpret_cast<const nfdpathset_t*>(pathSet.release());
        return NFD_OKAY;
    }

    nfdresult_t CollectFirstInstallPath(nfdnchar_t** outPath, bool includeFiles, bool includeDirectories)
    {
        ClearOutputPath(outPath);

        const nfdpathset_t* rawPathSet = nullptr;
        nfdresult_t result = CollectInstallPaths(&rawPathSet, includeFiles, includeDirectories);
        if (result != NFD_OKAY)
        {
            return result;
        }

        auto* pathSet = reinterpret_cast<const SwitchPathSet*>(rawPathSet);
        result = SetSinglePath(outPath, pathSet->paths.front());
        delete pathSet;
        return result;
    }

    nfdresult_t CancelSinglePath(nfdnchar_t** outPath)
    {
        ClearOutputPath(outPath);
        return NFD_CANCEL;
    }
}

void NFD_FreePathN(nfdnchar_t* filePath)
{
    std::free(filePath);
}

void NFD_FreePathU8(nfdu8char_t* filePath)
{
    std::free(filePath);
}

nfdresult_t NFD_Init(void)
{
    g_lastError.clear();
    return NFD_OKAY;
}

void NFD_Quit(void)
{
}

nfdresult_t NFD_OpenDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*)
{
    return CollectFirstInstallPath(outPath, true, false);
}

nfdresult_t NFD_OpenDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*)
{
    return CollectFirstInstallPath(outPath, true, false);
}

nfdresult_t NFD_OpenDialogN_With_Impl(nfdversion_t, nfdnchar_t** outPath, const nfdopendialognargs_t*)
{
    return CollectFirstInstallPath(outPath, true, false);
}

nfdresult_t NFD_OpenDialogU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdopendialogu8args_t*)
{
    return CollectFirstInstallPath(outPath, true, false);
}

nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** outPaths, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*)
{
    return CollectInstallPaths(outPaths, true, false);
}

nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t** outPaths, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*)
{
    return CollectInstallPaths(outPaths, true, false);
}

nfdresult_t NFD_OpenDialogMultipleN_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdopendialognargs_t*)
{
    return CollectInstallPaths(outPaths, true, false);
}

nfdresult_t NFD_OpenDialogMultipleU8_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdopendialogu8args_t*)
{
    return CollectInstallPaths(outPaths, true, false);
}

nfdresult_t NFD_SaveDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t*, nfdfiltersize_t, const nfdnchar_t*, const nfdnchar_t*)
{
    return CancelSinglePath(outPath);
}

nfdresult_t NFD_SaveDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*, const nfdu8char_t*)
{
    return CancelSinglePath(outPath);
}

nfdresult_t NFD_SaveDialogN_With_Impl(nfdversion_t, nfdnchar_t** outPath, const nfdsavedialognargs_t*)
{
    return CancelSinglePath(outPath);
}

nfdresult_t NFD_SaveDialogU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdsavedialogu8args_t*)
{
    return CancelSinglePath(outPath);
}

nfdresult_t NFD_PickFolderN(nfdnchar_t** outPath, const nfdnchar_t*)
{
    return CollectFirstInstallPath(outPath, false, true);
}

nfdresult_t NFD_PickFolderU8(nfdu8char_t** outPath, const nfdu8char_t*)
{
    return CollectFirstInstallPath(outPath, false, true);
}

nfdresult_t NFD_PickFolderN_With_Impl(nfdversion_t, nfdnchar_t** outPath, const nfdpickfoldernargs_t*)
{
    return CollectFirstInstallPath(outPath, false, true);
}

nfdresult_t NFD_PickFolderU8_With_Impl(nfdversion_t, nfdu8char_t** outPath, const nfdpickfolderu8args_t*)
{
    return CollectFirstInstallPath(outPath, false, true);
}

nfdresult_t NFD_PickFolderMultipleN(const nfdpathset_t** outPaths, const nfdnchar_t*)
{
    return CollectInstallPaths(outPaths, false, true);
}

nfdresult_t NFD_PickFolderMultipleU8(const nfdpathset_t** outPaths, const nfdu8char_t*)
{
    return CollectInstallPaths(outPaths, false, true);
}

nfdresult_t NFD_PickFolderMultipleN_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdpickfoldernargs_t*)
{
    return CollectInstallPaths(outPaths, false, true);
}

nfdresult_t NFD_PickFolderMultipleU8_With_Impl(nfdversion_t, const nfdpathset_t** outPaths, const nfdpickfolderu8args_t*)
{
    return CollectInstallPaths(outPaths, false, true);
}

const char* NFD_GetError(void)
{
    return g_lastError.empty() ? nullptr : g_lastError.c_str();
}

void NFD_ClearError(void)
{
    g_lastError.clear();
}

nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* pathSet, nfdpathsetsize_t* count)
{
    if (pathSet == nullptr || count == nullptr)
    {
        SetError("Invalid path set.");
        return NFD_ERROR;
    }

    *count = static_cast<nfdpathsetsize_t>(reinterpret_cast<const SwitchPathSet*>(pathSet)->paths.size());
    return NFD_OKAY;
}

nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdnchar_t** outPath)
{
    ClearOutputPath(outPath);

    auto* switchPathSet = reinterpret_cast<const SwitchPathSet*>(pathSet);
    if (switchPathSet == nullptr || outPath == nullptr || index >= switchPathSet->paths.size())
    {
        SetError("Invalid path set index.");
        return NFD_ERROR;
    }

    return SetSinglePath(outPath, switchPathSet->paths[index]);
}

nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdu8char_t** outPath)
{
    return NFD_PathSet_GetPathN(pathSet, index, outPath);
}

void NFD_PathSet_FreePathN(const nfdnchar_t* filePath)
{
    std::free(const_cast<nfdnchar_t*>(filePath));
}

void NFD_PathSet_FreePathU8(const nfdu8char_t* filePath)
{
    std::free(const_cast<nfdu8char_t*>(filePath));
}

nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t* pathSet, nfdpathsetenum_t* outEnumerator)
{
    if (pathSet == nullptr || outEnumerator == nullptr)
    {
        SetError("Invalid path set enumerator.");
        return NFD_ERROR;
    }

    outEnumerator->ptr = new SwitchPathSetEnumerator { reinterpret_cast<const SwitchPathSet*>(pathSet), 0 };
    return NFD_OKAY;
}

void NFD_PathSet_FreeEnum(nfdpathsetenum_t* enumerator)
{
    if (enumerator != nullptr)
    {
        delete reinterpret_cast<SwitchPathSetEnumerator*>(enumerator->ptr);
        enumerator->ptr = nullptr;
    }
}

nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t* enumerator, nfdnchar_t** outPath)
{
    ClearOutputPath(outPath);

    if (enumerator == nullptr || outPath == nullptr)
    {
        SetError("Invalid path set enumerator.");
        return NFD_ERROR;
    }

    auto* state = reinterpret_cast<SwitchPathSetEnumerator*>(enumerator->ptr);
    if (state == nullptr || state->pathSet == nullptr || state->index >= state->pathSet->paths.size())
    {
        return NFD_OKAY;
    }

    return SetSinglePath(outPath, state->pathSet->paths[state->index++]);
}

nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t* enumerator, nfdu8char_t** outPath)
{
    return NFD_PathSet_EnumNextN(enumerator, outPath);
}

void NFD_PathSet_Free(const nfdpathset_t* pathSet)
{
    delete reinterpret_cast<const SwitchPathSet*>(pathSet);
}