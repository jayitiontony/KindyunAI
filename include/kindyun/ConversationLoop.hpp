/**
 * @file ConversationLoop.hpp
 * @brief 对话循环类声明 —— Agent 的决策引擎
 *
 * ConversationLoop 是整个 Agent 系统最核心的组件，负责管理完整的对话生命周期：
 *   1. 用户输入 → 构建消息上下文 → 调用 LLM 获取响应
 *   2. 检测 LLM 响应中的工具调用 → 执行工具 → 将结果回传 LLM
 *   3. 重复步骤 2，直到 LLM 返回最终文本响应
 *   4. 上下文压缩：当对话过长时自动滑动窗口压缩，防止上下文溢出
 *
 * 核心循环（ReAct 范式）：
 *   User Input → [LLM] → Tool Call(s) → [Execute Tool(s)] → [LLM] → ... → Final Response
 *
 * @see LLMClient 为 LLM 通信层
 * @see ToolDispatcher 为工具分发层
 * @see ConversationLoop.cpp 为实现文件
 */

#pragma once
#include <string>
#include <vector>
#include <functional>
#include "kindyun/Types.hpp"
#include "kindyun/Config.hpp"

// 前置声明 —— 避免循环包含
class LLMClient;
class ToolDispatcher;
class ApprovalManager;
class MemoryDB;

/**
 * @brief 流式事件类型 —— ConversationLoop 通过统一回调推送
 *
 * 让前端能区分：
 *   - Text        : LLM 正在生成文本片段（chunk）
 *   - ToolPending : LLM 决定调用工具（name + args）
 *   - ToolResult  : 工具执行完毕（result + is_error）
 *   - Done        : 整轮对话结束
 */
struct StreamEvent {
    enum Type { Text, ToolPending, ToolResult, Done };
    Type type = Text;

    // Text
    std::string text;

    // ToolPending / ToolResult 共用字段
    std::string tool_call_id;
    std::string tool_name;
    nlohmann::json tool_args;       // ToolPending
    std::string tool_result;        // ToolResult
    bool is_error = false;
};

/**
 * @brief 事件流回调：同时支持文本片段、工具调用、工具结果
 *
 * 推荐使用 —— 前端可区分 LLM 文本和工具调用事件，UI 上能展开/折叠工具详情。
 *
 * 注：此类型与 LLMClient::StreamCallback（仅文本片段）名字相同但语义不同。
 *     ConversationLoop::runStream 接受此类型；内部把 LLMClient 的文本回调
 *     适配为本事件回调。
 */
using StreamEventCallback = std::function<bool(const StreamEvent&)>;

/**
 * @class ConversationLoop
 * @brief 对话循环 —— Agent 的核心决策引擎
 *
 * ConversationLoop 管理完整的对话生命周期，实现 ReAct (Reasoning + Acting) 范式：
 *   1. 接收用户输入，构建消息上下文
 *   2. 调用 LLM 获取响应（工具调用或文本）
 *   3. 如果 LLM 返回工具调用，执行工具并将结果回传 LLM
 *   4. 循环直到 LLM 返回最终文本响应
 *
 * 上下文压缩策略：
 *   - 当 LLM 返回 ContextOverflow 时，自动调用 compressContext()
 *   - compressContext() 使用滑动窗口策略保留最近的消息
 *
 * @note 此类为单线程设计，多线程使用需外部同步。
 */
class ConversationLoop {
public:
    /**
     * @brief 构造函数 —— 初始化对话循环组件
     *
     * @param llm LLM 通信客户端引用
     * @param dispatcher 工具分发器引用
     * @param config 配置管理器引用
     */
    ConversationLoop(
        LLMClient& llm,
        ToolDispatcher& dispatcher,
        const Config& config
    );

    /**
     * @brief 非流式运行单轮对话（同步模式）
     * @param userInput 用户的自然语言输入
     * @return LLM 的最终文本响应
     */
    std::string run(const std::string& userInput);

    /**
     * @brief 流式运行一轮对话（事件模式）
     * @param userInput 用户的自然语言输入
     * @param outputCallback 事件回调（Text / ToolPending / ToolResult）
     * @return LLM 的完整响应文本
     */
    std::string runStream(const std::string& userInput,
                          StreamEventCallback outputCallback = nullptr);

    /**
     * @brief 获取当前消息历史引用（只读）
     * @return 当前对话历史的 const 引用
     */
    const std::vector<Message>& history() const;

    /**
     * @brief 清空消息历史和相关状态
     */
    void clearHistory();

    /**
     * @brief 保存会话到文件
     * @param path 保存路径
     */
    void saveSession(const std::string& path) const;

    /**
     * @brief 从文件加载会话
     * @param path 会话文件路径
     */
    void loadSession(const std::string& path);

    /**
     * @brief 设置系统提示词
     * @param prompt 新的系统提示内容
     */
    void setSystemPrompt(const std::string& prompt);

    /**
     * @brief 注入审批管理器
     * @param mgr 审批管理器指针
     */
    void setApprovalManager(ApprovalManager* mgr);

    /**
     * @brief 注入记忆数据库
     * @param db 记忆数据库指针
     */
    void setMemoryDB(MemoryDB* db);

private:
    /**
     * @brief 内部：执行工具调用循环
     * @param tools 已启用工具的定义列表
     * @return LLM 的最终文本响应
     */
    std::string executeToolLoop(const std::vector<ToolDefinition>& tools);

    /**
     * @brief 内部：流式执行工具调用循环
     * @param tools 已启用工具的定义列表
     * @param outputCallback 事件回调
     * @return LLM 的完整响应文本
     */
    std::string executeToolLoopStream(const std::vector<ToolDefinition>& tools,
                                      StreamEventCallback outputCallback);

    /**
     * @brief 内部：检查是否需要上下文压缩
     * @return true 如果需要压缩
     */
    bool shouldCompress() const;

    /**
     * @brief 内部：执行滑动窗口上下文压缩
     */
    void compressContext();

    /**
     * @brief 内部：处理单个工具调用
     * @param tc 工具调用信息
     * @return ToolResult 结果
     */
    ToolResult handleToolCall(const ToolCall& tc);

    /**
     * @brief 内部：调试用——打印完整的对话上下文和 LLM 响应
     * @param msgs 当前的消息上下文
     * @param response_content LLM 返回的原始文本响应
     */
    void logConversationDebug(const std::vector<Message>& msgs, const std::string& response_content) const;

    /**
     * @brief 内部：计算当前上下文的总 token 数（估算值）
     * @return 当前上下文的估算 token 总数
     */
    size_t getContextTokenCount() const;

private:
    // 外部依赖
    LLMClient& m_llm;
    ToolDispatcher& m_dispatcher;
    const Config& m_config;

    // 对话状态
    std::vector<Message> m_history;
    std::string m_systemPrompt;

    // 控制参数
    int m_iterationCount = 0;
    int m_emptyResponseRetries = 0;
    int m_maxIterations = 50;
    int m_emptyResponseMaxRetries = 3;
    int m_consecutive_llm_failures = 0;

    // 插件/模块
    ApprovalManager* m_approval_mgr = nullptr;
    MemoryDB* m_memory_db = nullptr;
};
