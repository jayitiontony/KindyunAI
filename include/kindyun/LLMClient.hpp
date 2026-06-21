/**
 * @file LLMClient.hpp
 * @brief LLM HTTP客户端封装
 *
 * 封装与 LLM HTTP API 的通信，支持：
 *   - 多轮对话上下文管理
 *   - OpenAI-compatible function calling / tool_calls
 *   - API Key 认证（Bearer Token）
 *   - 自动重试与错误处理
 *
 * 设计说明：
 *   - 使用 libcurl 进行 HTTP 通信（C++ 无标准网络库）
 *   - 请求/响应体使用 nlohmann/json 序列化
 *   - 对话历史由调用方（ConversationLoop）管理，本类只负责单次请求
 */

#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "nlohmann/json.hpp"
#include "kindyun/Types.hpp"

using json = nlohmann::json;

/**
 * @brief 流式响应回调函数类型（LLMClient 内部使用，只发文本 chunk）
 *
 * 注：ConversationLoop 暴露更丰富的 StreamEvent 回调（含 tool_pending/tool_result）
 *     供前端区分。LLMClient 这一层只关心文本片段。
 *
 * @param chunk 收到的文本片段
 * @return true 继续接收，false 停止接收
 */
using StreamCallback = std::function<bool(const std::string& chunk)>;

/**
 * @brief LLM 错误类型枚举
 */
enum class LLMErrorType {
    NetworkError,      // curl 连接失败
    Timeout,           // 请求超时
    AuthError,         // 401/403
    RateLimit,         // 429
    ServerError,       // 500/502/503
    ParseError,        // JSON 解析失败
    ContextOverflow,   // context too long
    Unknown
};

/**
 * @brief LLM 错误信息结构
 */
struct LLMError {
    LLMErrorType type = LLMErrorType::Unknown;
    std::string message;
    int http_code = 0;
};

/**
 * @brief LLM HTTP 客户端
 *
 * 封装 OpenAI-compatible /v1/chat/completions 接口。
 * 支持 function calling（工具调用）和多轮对话历史。
 */
class LLMClient {
public:
    /**
     * @brief 构造客户端
     * @param url LLM 服务基础 URL（不含 /v1/chat/completions 后缀）
     * @param model    模型名称
     */
    LLMClient(const std::string& url, const std::string& model);

    /**
     * @brief 设置 API Key（可选）
     * @param apiKey   Bearer Token，若设置则每次请求带上 Authorization 头
     */
     void setApiKey(const std::string& apiKey);

    /**
     * @brief 设置 HTTP 请求超时（秒）
     * @param timeout 普通请求超时秒数（对应配置 http.curl_timeout_seconds）
     */
    void setTimeout(int timeout);

    /**
     * @brief 设置流式响应超时（秒）
     * @param timeout 流式响应超时秒数（对应配置 http.stream_timeout_seconds）
     */
    void setStreamTimeout(int timeout);

    // ===== 多轮对话接口（主接口）=====
    /**
     * @brief 多轮对话请求
     * @param messages  当前对话上下文（含历史）
     * @param tools     工具定义列表（发给 LLM，用于 function calling）
     * @return          LLM 响应（可能含 tool_calls 或纯文本）
     */
    LLMResponse chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {}
    );

    // ===== 流式响应接口 =====
    /**
     * @brief 流式多轮对话请求
     * @param messages  当前对话上下文（含历史）
     * @param tools     工具定义列表
     * @param callback  每个文本片段的回调，返回 false 可中止
     * @return          LLM 完整响应（可能含 tool_calls）
     */
    LLMResponse generateStream(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools,
        StreamCallback callback
    );

    // ===== 兼容接口（保留，Phase 1 过渡用）=====
    /**
     * @brief 简单对话（单轮，不支持工具调用）
     * @param prompt 用户输入
     * @param history  历史对话文本（拼成 string，Phase 1 用）
     * @return         LLM 回复文本
     */
    std::string chat(const std::string& prompt, const std::string& history = "");

    // ===== 错误处理接口 =====
    /**
     * @brief 获取最近一次错误信息
     * @return 最近发生的错误
     */
    LLMError getLastError() const;

    /**
     * @brief 获取本次请求的重试次数
     * @return 重试次数
     */
    int getRetryCount() const;

    /**
     * @brief 清除错误状态
     */
    void clearError();

private:
    std::string m_base_url;
    std::string m_model_name;
    std::string m_api_key;
    LLMError m_last_error;
    int m_retry_count = 0;
    int m_timeout = 120;       // curl 普通请求超时（秒）
    int m_stream_timeout = 300; // curl 流式响应超时（秒）

    /// 内部：发送 HTTP POST 请求（带重试）
    json sendRequestWithRetry(const json& body);

    /// 内部：发送单个 HTTP POST 请求
    json sendRequest(const json& body);

    /// 内部：根据 HTTP 状态码分类错误类型
    static LLMErrorType classifyError(long http_code, const std::string& message);

    /// 内部：解析 OpenAI API JSON 响应
    LLMResponse parseResponse(const json& response) const;

    /// 内部：构造 messages（兼容接口用）
    std::vector<Message> buildMessages(const std::string& prompt, const std::string& history) const;
};