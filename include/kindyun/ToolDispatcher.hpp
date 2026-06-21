/**
 * @file ToolDispatcher.hpp
 * @brief 工具分发器 —— LLM 工具调用与实际执行之间的桥梁
 *
 * ToolDispatcher 是工具执行链路的核心调度层，负责：
 *   1. 接收来自 LLM 响应的 ToolCall 对象
 *   2. 查找并执行对应的工具实现（通过 ToolRegistry）
 *   3. 对执行结果进行编码转换和长度截断
 *   4. 返回标准化的 ToolResult 供 ConversationLoop 回传 LLM
 *
 * 执行链路：
 *   LLMResponse.tool_calls → ConversationLoop::executeToolLoop()
 *   → ToolDispatcher::dispatch() → ToolRegistry::executeTool() → ToolHandler → ToolResult
 *
 * @note 本组件是无状态的，不持有任何实例数据。所有工具状态通过 ToolRegistry 单例管理。
 */
#pragma once
#ifndef KINDYUN_TOOL_DISPATCHER_HPP
#define KINDYUN_TOOL_DISPATCHER_HPP

#include <string>
#include "nlohmann/json.hpp"
#include "kindyun/Types.hpp"

using json = nlohmann::json;

/**
 * @brief 工具分发器
 *
 * 负责将 LLM 的工具调用请求（ToolCall）分发给对应的工具实现，
 * 并处理结果编码和大小截断。
 *
 * @note 该类为纯工具类（无状态），不持有成员变量。
 *       所有工具注册和状态管理通过 ToolRegistry 单例进行。
 */
class ToolDispatcher {
public:
    /**
     * @brief 分发一个工具调用请求
     *
     * 核心工作流：
     *   1. 从 ToolCall 中提取工具名称和参数
     *   2. 通过 ToolRegistry::executeTool() 查找并执行对应工具
     *   3. 将结果编码转换为 UTF-8（处理 Windows GBK 编码问题）
     *   4. 对过长结果进行截断，防止 LLM 上下文溢出
     *   5. 封装为标准化的 ToolResult 返回
     *
     * @param toolCall 来自 LLM 响应的工具调用请求，包含工具名称、参数和调用 ID
     * @return ToolResult 执行结果，包含 tool_call_id、结果内容和错误标志
     *
     * @see ToolRegistry::executeTool()
     * @see ToolResult
     */
    ToolResult dispatch(const ToolCall& toolCall);

    /**
     * @brief 截断工具执行结果，防止超出上下文窗口
     *
     * 当工具执行返回的内容超过 MAX_RESULT_CHARS 限制时，
     * 截断内容并添加 "... (truncated)" 标记。
     *
     * @param result 原始工具执行结果
     * @param maxChars 最大字符数限制（默认 MAX_RESULT_CHARS = 100000）
     * @return 截断后的结果字符串（不超过 maxChars 字符）
     *
     * @note 截断策略为简单的前缀保留：只保留前 maxChars 个字符。
     */
    static std::string truncateResult(const std::string& result, size_t maxChars = 100000);

    /**
     * @brief 工具输出大小限制（硬编码常量）
     *
     * 工具执行结果的最大字符数。超过此限制的内容将被截断，
     * 防止超长输出导致 LLM 上下文窗口溢出。
     *
     * @note 当前值为 100,000 字符。如果工具可能返回大量数据，
     *       可考虑通过 Config 配置此值。
     */
    static const size_t MAX_RESULT_CHARS = 100000;
};

#endif // KINDYUN_TOOL_DISPATCHER_HPP