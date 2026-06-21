/**
 * @file Config.hpp
 * @brief 配置管理器
 *
 * 统一管理系统配置，支持从 JSON 文件加载和运行时访问。
 * 使用单例模式，全局唯一配置实例。
 *
 * 配置分层：
 *   1. 加载 config.json（可选）
 *   2. 未找到文件时使用内置默认值
 *
 * 主要配置分组：
 *   - llm      : LLM 连接配置（URL、模型、温度等）
 *   - agent    : Agent 行为配置（迭代次数、重试策略等）
 *   - tools    : 工具集启用配置
 *   - security : 安全限制配置（路径黑名单、文件大小限制等）
 */

#pragma once
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class Config {
public:
    /// 获取单例实例
    static Config& instance();

    /// 从 JSON 文件加载配置（路径不存在时静默使用默认值）
    bool load(const std::string& path);

    // ===== 通用访问器（带默认值）=====
    /// 按点分隔路径读取字符串，如 "llm.model"
    std::string getStr(const std::string& key, const std::string& fallback = "") const;
    /// 按点分隔路径读取整数
    int getInt(const std::string& key, int fallback = 0) const;
    /// 按点分隔路径读取布尔值
    bool getBool(const std::string& key, bool fallback = false) const;
    /// 按点分隔路径读取 JSON 对象
    json getJson(const std::string& key) const;

    /// 获取 tools.approval_required 列表（Phase 2 审批用）
    std::vector<std::string> getApprovalRequired() const;

    // ===== LLM 配置 =====
    /// LLM 服务地址（不含 /v1/chat/completions 后缀）
    std::string llm_url() const;
    /// 模型名称
    std::string llm_model() const;
    /// 最大生成 token 数
    int llm_max_tokens() const;
    /// 生成温度（0~2，越高越随机）
    double llm_temperature() const;
    /// 是否启用对话调试模式（打印所有 Prompt 和 Response）
    bool debug_conversation() const;

    // ===== 上下文压缩配置（Phase 3/滑动窗口）=====
    /// 最大上下文 token 数（超过此值触发压缩）
    size_t max_context_tokens() const;
    /// 压缩触发阈值比例（实际触发阈值 = max_context_tokens * ratio）
    double compress_threshold_ratio() const;
    /// 压缩时保留的最近消息数
    int preserve_recent_messages() const;
    /// 滑动窗口目标 token 数（压缩后尽量不超过此值）
    size_t sliding_window_tokens() const;
    /// 是否启用 LLM 摘要（true=生成真实摘要，false=仅丢弃旧消息）
    bool enable_summarization() const;

    // ===== Agent 行为配置 =====
    /// 最大工具调用迭代次数（防止死循环）
    int max_iterations() const;
    /// 空响应重试次数上限
    int empty_response_retries() const;

    // ===== 工具集配置 =====
    /// 当前启用的工具集列表（未配置的默认全部启用）
    std::vector<std::string> enabled_toolsets() const;
    /// 明确禁用的工具名列表（优先级高于 enabled_toolsets）
    std::vector<std::string> disabled_tools() const;

    // ===== 安全配置 =====
    /// 单次文件读取最大字符数（超过截断）
    size_t max_file_read_chars() const;
    /// 终端命令最大执行时间（秒）
    int terminal_timeout_seconds() const;
    /// 禁止访问的路径列表
    std::vector<std::string> protected_paths() const;

    // ===== HTTP 配置 =====
    /// curl 普通请求超时（秒）
    int http_curl_timeout() const;
    /// curl 流式响应超时（秒）
    int http_stream_timeout() const;

    // ===== Web 服务配置（KindyunAIServer.exe 使用）=====
    /// 是否启用 Web 服务
    bool web_enabled() const;
    /// 监听地址（如 "127.0.0.1"）
    std::string web_host() const;
    /// 监听端口
    int web_port() const;
    /// API 鉴权 Token（空 = 禁用鉴权）
    std::string web_api_token() const;
    /// 是否启用对外服务接口 (/api/v1/external/*)
    bool web_enable_external_api() const;
    /// CORS 允许的来源列表
    std::vector<std::string> web_cors_allow_origins() const;
    /// 静态资源目录
    std::string web_static_dir() const;
    /// 模板目录
    std::string web_templates_dir() const;
    /// 速率限制：每分钟每 token 最多请求数
    int web_rate_limit_rpm() const;
    /// 会话空闲超时（秒）
    int web_session_idle_ttl_seconds() const;
    /// 最大并发会话数
    int web_session_max_concurrent() const;
    /// 是否将会话历史落盘
    bool web_session_persist_on_disk() const;
    /// 服务器专用数据库路径
    std::string web_session_db_path() const;
    /// 审批模式："auto" | "interactive"
    std::string web_approval_mode() const;
    /// 日志级别："debug" | "info" | "warn" | "error"
    std::string web_log_level() const;
    /// 是否启用 access log
    bool web_access_log() const;
    /// 完整 web 子配置对象（用于外部服务等复杂子配置）
    json web_external_services_config() const;

private:
    Config() = default;

    /// 内部配置存储（JSON 对象，支持点分隔键访问）
    json m_config;
};