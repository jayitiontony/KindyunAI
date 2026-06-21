#pragma once
#ifndef KINDYUN_PLUGIN_HPP
#define KINDYUN_PLUGIN_HPP

#include <string>
#include <functional>
#include <vector>
#include "nlohmann/json.hpp"
#include <windows.h>

using json = nlohmann::json;

// ============================================================================
// 插件导出宏（DLL 导入/导出）
// ============================================================================

#ifdef KINDYUN_PLUGIN_EXPORTS
    #define KINDYUN_PLUGIN_API extern "C" __declspec(dllexport)
#else
    #define KINDYUN_PLUGIN_API extern "C" __declspec(dllimport)
#endif

// ============================================================================
// 插件接口定义
// ============================================================================

/**
 * @brief KindyunAI 插件标准接口
 *
 * 所有 KindyunAI 插件 DLL 必须导出以下函数：
 *   - get_plugin_name()      : 返回插件名称
 *   - get_tool_name()        : 返回工具名称（唯一标识）
 *   - get_tool_description() : 返回工具功能描述（用于 LLM 提示）
 *   - get_tool_parameters()  : 返回 JSON Schema 格式的参数定义
 *   - execute_tool(args)     : 执行工具，返回结果字符串
 *   - cleanup_tool()         : 清理插件资源（可选，无操作时为空实现）
 *
 * @note 插件 DLL 必须遵循严格的命名规范：KindyunPlugin[名称].dll
 *       例如：KindyunPluginFileSearch.dll、KindyunPluginNetwork.dll
 */

/**
 * @brief 获取插件名称
 * @return 插件名称字符串（如 "FileSearch"、"NetworkUtil"）
 */
typedef const char* (*PFN_GET_PLUGIN_NAME)();

/**
 * @brief 获取工具名称
 * @return 工具名称字符串（全局唯一，如 "file_search"）
 */
typedef const char* (*PFN_GET_TOOL_NAME)();

/**
 * @brief 获取工具描述
 * @return 工具功能描述（发送给 LLM，用于工具选择决策）
 */
typedef const char* (*PFN_GET_TOOL_DESCRIPTION)();

/**
 * @brief 获取工具参数定义（JSON Schema 格式）
 * @return JSON 字符串，描述工具的参数结构
 */
typedef const char* (*PFN_GET_TOOL_PARAMETERS)();

/**
 * @brief 执行工具
 * @param arguments JSON 字符串格式的参数
 * @return 工具执行结果字符串
 */
typedef const char* (*PFN_EXECUTE_TOOL)(const char* arguments);

/**
 * @brief 清理插件资源（可选）
 */
typedef void (*PFN_CLEANUP_TOOL)();

// ============================================================================
// 插件加载器
// ============================================================================

/**
 * @brief 插件加载器 - 负责加载和管理 KindyunAI 插件 DLL
 *
 * 功能：
 *   1. 扫描指定目录下符合命名规范（KindyunPlugin*.dll）的 DLL 文件
 *   2. 加载 DLL 并导出所需的函数指针
 *   3. 向 ToolRegistry 注册插件工具
 *   4. 管理插件的生命周期（加载、执行、卸载）
 *
 * @note 使用单例模式管理插件实例
 */
class PluginLoader {
public:
    /// 获取全局唯一的 PluginLoader 实例
    static PluginLoader& instance();

    /**
     * @brief 加载所有插件
     *
     * 遍历指定目录（默认当前工作目录），查找符合命名规范的 DLL 文件，
     * 加载每个 DLL 并注册其提供的工具到 ToolRegistry。
     *
     * @param directory 要扫描的目录路径（默认为当前工作目录）
     * @param toolset   插件工具所属的工具集名称（默认为 "plugin"）
     * @param requires_approval 是否需要用户确认后执行（默认 true）
     */
    void loadPlugins(const std::string& directory = ".",
                     const std::string& toolset = "plugin",
                     bool requires_approval = true);

    /**
     * @brief 卸载所有插件
     *
     * 调用每个插件的 cleanup_tool 函数，并释放 DLL 模块句柄。
     */
    void unloadAll();

    /**
     * @brief 获取已加载插件的数量
     * @return 已加载插件的数量
     */
    size_t pluginCount() const { return m_plugins.size(); }

private:
    /**
     * @brief 插件实例结构体
     *
     * 存储已加载插件的所有信息，包括 DLL 句柄、函数指针和名称。
     */
    struct PluginInstance {
        HMODULE hModule = nullptr;                    ///< DLL 模块句柄
        std::string name;                             ///< 插件名称
        std::string toolName;                         ///< 工具名称
        std::string description;                      ///< 工具描述
        std::string parameters;                       ///< 参数 JSON 字符串
        PFN_GET_PLUGIN_NAME get_plugin_name = nullptr;
        PFN_GET_TOOL_NAME get_tool_name = nullptr;
        PFN_GET_TOOL_DESCRIPTION get_tool_description = nullptr;
        PFN_GET_TOOL_PARAMETERS get_tool_parameters = nullptr;
        PFN_EXECUTE_TOOL execute_tool = nullptr;
        PFN_CLEANUP_TOOL cleanup_tool = nullptr;
    };

    std::vector<PluginInstance> m_plugins;  ///< 已加载的插件列表
};

#endif // KINDYUN_PLUGIN_HPP
