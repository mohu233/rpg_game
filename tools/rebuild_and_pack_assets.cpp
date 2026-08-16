#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#ifndef RPG_SOURCE_ASSET_DIR
#define RPG_SOURCE_ASSET_DIR L"assets"
#endif

#ifndef RPG_PACKAGED_ASSET_DIR
#define RPG_PACKAGED_ASSET_DIR L"assets"
#endif

namespace {

bool IsInside(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto relative = child.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != L"..";
}

bool CopyAssetFile(
    const std::filesystem::path& from,
    const std::filesystem::path& to,
    std::error_code& error) {
    std::filesystem::create_directories(to.parent_path(), error);
    if (error) return false;
    if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    return true;
}

bool CopyAssetTree(
    const std::filesystem::path& fromRoot,
    const std::filesystem::path& toRoot,
    std::uintmax_t& copiedFiles,
    std::filesystem::path& failedSource,
    std::filesystem::path& failedTarget,
    std::error_code& error) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(fromRoot, error)) {
        if (error) return false;
        if (!entry.is_regular_file()) continue;

        const std::filesystem::path relative = entry.path().lexically_relative(fromRoot);
        const std::filesystem::path target = toRoot / relative;
        failedSource = entry.path();
        failedTarget = target;

        if (!CopyAssetFile(entry.path(), target, error)) return false;
        ++copiedFiles;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path source = std::filesystem::path(RPG_SOURCE_ASSET_DIR).lexically_normal();
    const std::filesystem::path destination = std::filesystem::path(RPG_PACKAGED_ASSET_DIR).lexically_normal();
    const std::filesystem::path buildRoot = destination.parent_path();

    if (!std::filesystem::is_directory(source)) {
        std::cerr << "Asset source directory does not exist: " << source.u8string() << '\n';
        return 1;
    }
    if (source == destination || !IsInside(destination, buildRoot)) {
        std::cerr << "Refusing to replace an unsafe asset destination.\n";
        return 1;
    }

    std::error_code error;
    std::filesystem::remove_all(destination, error);
    if (error) {
        std::cerr << "Failed to clear packaged asset directory: " << error.message() << '\n';
        return 1;
    }
    std::filesystem::create_directories(destination, error);
    if (error) {
        std::cerr << "Failed to create packaged asset directory: " << error.message() << '\n';
        return 1;
    }

    std::filesystem::path failedSource;
    std::filesystem::path failedTarget;
    std::uintmax_t copiedFiles = 0;
    if (!CopyAssetTree(source, destination, copiedFiles, failedSource, failedTarget, error)) {
        std::cerr << "Failed to package assets: " << error.message() << '\n'
                  << "Source: " << failedSource.u8string() << '\n'
                  << "Target: " << failedTarget.u8string() << '\n';
        return 1;
    }

    std::uintmax_t fileCount = 0;
    std::uintmax_t pngCount = 0;
    std::uintmax_t totalBytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(destination, error)) {
        if (error) break;
        if (!entry.is_regular_file()) continue;
        ++fileCount;
        totalBytes += entry.file_size();
        if (entry.path().extension() == L".png") ++pngCount;
    }
    if (error) {
        std::cerr << "Failed to verify packaged assets: " << error.message() << '\n';
        return 1;
    }

    std::cout << "Rebuild dependencies completed.\n"
              << "Source assets were packaged into a clean build directory.\n"
              << "Copied source files: " << copiedFiles << '\n'
              << "Packaged assets: " << destination.u8string() << '\n'
              << "Files: " << fileCount << ", PNG textures: " << pngCount
              << ", bytes: " << totalBytes << '\n';
    return 0;
}
