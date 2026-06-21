/**
 * @file PluginLoader.cpp
 * @brief KindyunAI 插件加载器实现
 *
 * 负责加载和管理 KindyunAI 插件 DLL 文件。
 * 插件 DLL 必须遵循命名规范（KindyunPlugin*.dll）并导出统一接口。
 */

#include "kindyun/KindyunPlugin.hpp"
#include "kindyun/ToolBase.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <windows.h>
#include <direct.h>
#include <dir.h>

// ============================================================================
// PluginLoader 实现
// ============================================================================

PluginLoader& PluginLoader::instance() {
    static PluginLoader instance;
    return instance;
}

void PluginLoader::loadPlugins(const std::string& directory,
                               const std::string& toolset,
                               bool requires_approval) {
    std::cout << "[PluginLoader] Scanning directory: " << directory << std::endl;
    std::cout << "[PluginLoader] Toolset: " << toolset << ", Approval: "
              << (requires_approval ? "yes" : "no") << std::endl;

    // 收集所有候选 DLL 路径（优先使用主目录，回退到常见子目录）
    std::vector<std::string> searchDirs;
    searchDirs.push_back(directory);

    // 尝试在常见子目录中查找（仅当主目录无结果时）
    std::vector<std::string> fallbackDirs = {"./bin/Release", "./bin/Debug",
                                             directory + "\\bin\\Release",
                                             directory + "\\bin\\Debug",
                                             directory + "/bin/Release",
                                             directory + "/bin/Debug"};

    std::filesystem::path matchedDir;
    bool foundAny = false;

    std::vector<std::filesystem::path> searchPaths;
    searchPaths.push_back(std::filesystem::path(directory));
    for (const auto& f : fallbackDirs) {
        searchPaths.push_back(std::filesystem::path(f));
    }

    for (const auto& p : searchPaths) {
        if (!std::filesystem::exists(p) || !std::filesystem::is_directory(p)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(p)) {
            auto ext = entry.path().extension();
            auto filename = entry.path().filename().string();
            if (ext == ".dll" && filename.find("KindyunPlugin") == 0) {
                matchedDir = p;
                foundAny = true;
                break;
            }
        }
        if (foundAny) break;
    }

    if (!foundAny) {
        std::cout << "[PluginLoader] No plugin DLLs found." << std::endl;
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(matchedDir)) {
        auto ext = entry.path().extension();
        auto filename = entry.path().filename().string();

        if (ext == ".dll" && filename.find("KindyunPlugin") == 0) {
            std::cout << "[PluginLoader] Loading plugin: " << filename << std::endl;
            std::string dllPath = entry.path().string();

            HMODULE hModule = LoadLibraryA(dllPath.c_str());
            if (!hModule) {
                std::cerr << "[PluginLoader] Failed to load: " << filename
                          << " (Error code: " << GetLastError() << ")" << std::endl;
                continue;
            }

            PluginInstance plugin;
            plugin.hModule = hModule;
            plugin.get_plugin_name = (PFN_GET_PLUGIN_NAME)GetProcAddress(hModule, "get_plugin_name");
            plugin.get_tool_name = (PFN_GET_TOOL_NAME)GetProcAddress(hModule, "get_tool_name");
            plugin.get_tool_description = (PFN_GET_TOOL_DESCRIPTION)GetProcAddress(hModule, "get_tool_description");
            plugin.get_tool_parameters = (PFN_GET_TOOL_PARAMETERS)GetProcAddress(hModule, "get_tool_parameters");
            plugin.execute_tool = (PFN_EXECUTE_TOOL)GetProcAddress(hModule, "execute_tool");
            plugin.cleanup_tool = (PFN_CLEANUP_TOOL)GetProcAddress(hModule, "cleanup_tool");

            if (!plugin.get_plugin_name || !plugin.get_tool_name ||
                !plugin.get_tool_description || !plugin.get_tool_parameters ||
                !plugin.execute_tool) {
                std::cerr << "[PluginLoader] Missing required exports in: " << filename << std::endl;
                FreeLibrary(hModule);
                continue;
            }

            plugin.name = plugin.get_plugin_name();
            plugin.toolName = plugin.get_tool_name();
            plugin.description = plugin.get_tool_description();
            plugin.parameters = plugin.get_tool_parameters();

            try {
                auto handler = [plugin](const json& args) -> std::string {
                    try {
                        const char* result = plugin.execute_tool(args.dump().c_str());
                        if (result) return std::string(result);
                        return "Error: Plugin returned null result";
                    } catch (const std::exception& e) {
                        return std::string("Plugin error: ") + e.what();
                    }
                };

                ToolRegistry::instance().registerTool(
                    plugin.toolName,
                    plugin.description,
                    json::parse(plugin.parameters, nullptr, false),
                    handler,
                    toolset,
                    requires_approval
                );

                std::cout << "[PluginLoader] Plugin loaded successfully: "
                          << plugin.name << " (Tool: " << plugin.toolName << ")" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[PluginLoader] Failed to register plugin '" << plugin.name << "': " << e.what() << std::endl;
            }
            m_plugins.push_back(plugin);
        }
    }

    std::cout << "[PluginLoader] Loaded " << m_plugins.size() << " plugin(s)." << std::endl;
}

void PluginLoader::unloadAll() {
    std::cout << "[PluginLoader] Unloading all plugins..." << std::endl;

    for (auto& plugin : m_plugins) {
        if (plugin.cleanup_tool) {
            plugin.cleanup_tool();
        }
        if (plugin.hModule) {
            FreeLibrary(plugin.hModule);
        }
        std::cout << "[PluginLoader] Unloaded: " << plugin.name << std::endl;
    }

    m_plugins.clear();
    std::cout << "[PluginLoader] All plugins unloaded." << std::endl;
}
