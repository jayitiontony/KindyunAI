#pragma once
#ifndef KINDYUN_TOOL_BASE_HPP
#define KINDYUN_TOOL_BASE_HPP

#include <string>
#include <functional>
#include <map>
#include <vector>
#include "nlohmann/json.hpp"
#include "kindyun/Types.hpp"

using json = nlohmann::json;

// ============================================================================
// 工具处理器类型别名
// ============================================================================

/**
 * @brief 工具执行函数类型定义
 *
 * 所有工具的核心执行逻辑都必须是此函数签名。
 * ToolRegistry 内部将此 std::function 存储并调用。
 *
 * @param arguments 解析后的工具参数（JSON 对象），其结构与 ToolDefinition::parameters 对应
 * @return 工具执行结果文本。执行成功时返回输出内容；失败时返回以 "Error: " 开头的错误描述
 *
 * @note 此函数应尽可能无副作用（除了工具本身的功能，如读文件）。
 *       不应抛出异常，因为 ToolRegistry::executeTool() 已做了 try-catch 保护。
 */
using ToolHandler = std::function<std::string(const json& arguments)>;

// ============================================================================
// 工具条目（注册表内部使用）
// ============================================================================

/**
 * @brief 工具注册条目
 *
 * 内部数据结构，存储已注册工具的完整元信息和执行器。
 * ToolRegistry 以 name 为 key 将此结构体存储在 std::map 中。
 *
 * @note handler 字段是一个 std::function，指向具体的工具实现逻辑。
 *       注册时通过 registerTool() 传入 lambda 或命名函数。
 */
struct ToolEntry {
    /// 工具名称（与 ToolDefinition::name 一致，如 "read_file"）
    std::string name;
    /// 工具功能描述（面向 LLM，帮助模型判断是否调用）
    std::string description;
    /// 参数 Schema（JSON Schema 格式），描述参数的类型、必填项和约束
    json parameters;
    /// 所属工具集名称（如 "core"、"edit"、"terminal"），用于分组管理
    std::string toolset;
    /// 是否需要用户确认后才执行（true 表示危险操作，如 write_file、terminal）
    bool requires_approval = false;
    /**
     * @brief 工具执行器（核心逻辑）
     *
     * 调用此函数即执行对应的工具功能。
     * 例如对于 "read_file"，handler 会打开文件、读取内容并返回。
     */
    ToolHandler handler;
};

// ============================================================================
// 工具注册中心（单例）
// ============================================================================

/**
 * @brief 工具注册中心（单例模式）
 *
 * 整个项目中只有一个 ToolRegistry 实例，负责：
 *   1. 存储所有已注册的工具（名称 → 实现）
 *   2. 按工具集（toolset）分组管理工具
 *   3. 接收 LLM 的工具调用请求并执行对应的工具
 *   4. 管理工具执行结果的回传
 *
 * @note 使用单例模式确保工具注册表的唯一性和全局可访问性。
 *       通过 ToolRegistry::instance() 获取实例。
 *
 * @see ToolBase.hpp 中的 ToolEntry、ToolHandler 类型定义
 * @see ConversationLoop::executeToolLoop() 了解工具调用的完整流程
 */
class ToolRegistry {
public:
    /// 获取全局唯一的单例实例（线程安全 C++11 静态局部变量）
    static ToolRegistry& instance();

    /**
     * @brief 注册一个新工具
     *
     * 将工具的名称、描述、参数 schema、执行器等信息注册到注册表。
     * 此函数在系统启动时通过 registerAllTools() 批量调用。
     *
     * @param name        工具唯一名称（如 "read_file"），全局不可重复
     * @param description 工具功能描述（发送给 LLM，帮助其决定是否调用）
     * @param parameters  参数 Schema（JSON Schema 格式），描述参数类型和约束
     * @param handler     工具执行函数（std::function），接收参数 JSON 并返回执行结果文本
     * @param toolset     所属工具集名称（默认 "core"），用于分组管理
     * @param requires_approval 是否需要用户确认后执行（默认 false）
     */
    void registerTool(
        const std::string& name,
        const std::string& description,
        const json& parameters,
        ToolHandler handler,
        const std::string& toolset = "core",
        bool requires_approval = false
    );

    /**
     * @brief 获取所有已注册工具的完整定义列表
     *
     * 将 ToolEntry 转换为 ToolDefinition 向量返回，
     * 通常用于在对话开始时发送给 LLM，告知其可用的工具。
     *
     * @return 工具定义向量，每个元素包含名称、描述、参数 schema、所属工具集
     */
    std::vector<ToolDefinition> getToolDefinitions() const;

    /**
     * @brief 按工具集获取工具定义
     *
     * 根据指定的工具集名称（如 "terminal"、"edit"）返回该组内的所有工具定义。
     * 用于 ConversationLoop 中按配置动态加载/禁用工具集。
     *
     * @param toolset 工具集名称（如 "core"、"terminal"、"edit"、"search"）
     * @return 指定工具集内的工具定义列表
     */
    std::vector<ToolDefinition> getToolDefinitionsInToolset(const std::string& toolset) const;

    /**
     * @brief 根据工具名称执行工具
     *
     * 在注册表中查找工具并执行其对应的 handler，返回执行结果。
     * 这是整个 Agent 系统执行工具的核心入口。
     *
     * @param name       要执行的工具名称
     * @param arguments  工具参数（解析后的 JSON 对象）
     * @param error      [输出参数] 如果执行出错，此处填入错误描述；可选
     * @return 工具执行结果文本
     */
    std::string executeTool(
        const std::string& name,
        const json& arguments,
        std::string* error = nullptr
    );

    /// 获取所有已注册的工具集名称列表
    std::vector<std::string> getToolsets() const;

    /// 获取指定工具集内的所有工具名称列表
    std::vector<std::string> getToolsInToolset(const std::string& toolset) const;

    /// 检查指定名称的工具是否已注册
    bool hasTool(const std::string& name) const;

    /**
     * @brief 安全检查：判断路径是否在黑名单中
     *
     * 遍历 BLOCKED_PATHS 列表，检查目标路径是否包含任何受保护路径。
     * 用于防止 LLM 误操作关键系统文件（如注册表、shadow 文件）。
     *
     * @param path 要检查的文件路径
     * @return true 表示路径安全，false 表示路径在黑名单中
     */
    static bool isPathSafe(const std::string& path);

    /// 系统受保护路径黑名单（用于 isPathSafe 检查）
    static const char* const BLOCKED_PATHS[];

    /**
     * @brief 注册所有内置工具
     *
     * 在系统启动时调用一次，将所有预定义工具注册到注册表中。
     * 此函数内部调用 registerTool() 多次，为每个内置工具添加条目。
     *
     * 内置工具包括：read_file、write_file、list_dir、patch、create_file、
     * remove_file、terminal、search_files、memory_tool、session_search、todo_tool 等。
     */
    void registerAllTools();

    /**
     * @brief 注入 MemoryDB 实例（用于持久化存储）
     *
     * 在系统初始化时将 MemoryDB 单例注入到 ToolRegistry，
     * 使得 memory_tool 和 session_search 工具可以访问底层数据库。
     *
     * @param db MemoryDB 实例指针（由系统初始化器设置）
     */
    void setMemoryDB(class MemoryDB* db);

    /// 获取已注入的 MemoryDB 实例指针
    class MemoryDB* memoryDB() const;

private:
    /// MemoryDB 实例指针，用于持久化存储（memory_tool / session_search 工具访问）
    class MemoryDB* m_memory_db = nullptr;

private:
    /// 私有构造函数，保证单例模式
    ToolRegistry();

    /**
     * @brief 内部工具注册表
     *
     * 以工具名称为 key，ToolEntry 为 value 的映射表。
     * ToolEntry 包含该工具的完整元信息和执行器。
     */
    std::map<std::string, ToolEntry> m_tools;

    /**
     * @brief 工具集分组索引
     *
     * 以工具集名称为 key，该工具集内的工具名称列表为 value。
     * 用于快速查找指定工具集下的所有工具，支持按组启用/禁用。
     */
    std::map<std::string, std::vector<std::string>> m_toolsets;  // toolset → tool names
};

#endif // KINDYUN_TOOL_BASE_HPP