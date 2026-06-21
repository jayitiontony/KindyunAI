/**
 * ============================================================================
 * KindyunAI - Web Service & External Interface Module
 * ============================================================================
 *
 * Copyright (c) 2026 Kindyun.com. All rights reserved.
 *
 * Website  : https://Kindyun.com
 * Author   : jayition
 * Email    : jayition@qq.com
 *
 * Part of the KindyunAI project. See VERSION.md for license and version info.
 * Unauthorized copying, modification, or distribution is prohibited.
 *
 * Version  : 1.0.0
 * ============================================================================
 */
/**
 * @file SessionManager.hpp
 * @brief 多会话管理器
 *
 * KindyunAIServer 用来隔离不同浏览器标签 / 不同 API 客户端的对话。
 *
 * 设计要点：
 *   - 每会话一个独立的 ConversationLoop 实例（堆分配）
 *   - 每会话一个独立的 LLMClient 实例（libcurl 句柄不跨线程共享）
 *   - SessionEntry 同时持有 client 和 loop，析构时按 client→loop 顺序释放
 *   - 共享 ToolDispatcher、Config、ApprovalManager、MemoryDB
 *   - shared_mutex 保护 map，并发读不加锁、修改加写锁
 *
 * 会话生命周期：
 *   createSession() → 生成 UUID → 入 map
 *   get(sid)        → 取出 ConversationLoop*（不存在返回 nullptr）
 *   destroy(sid)    → 析构 → 出 map
 */

#pragma once
#ifndef KINDYUN_SESSION_MANAGER_HPP
#define KINDYUN_SESSION_MANAGER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <chrono>
#include <mutex>
#include <atomic>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class ConversationLoop;
class ToolDispatcher;
class ApprovalManager;
class MemoryDB;
class Config;
class LLMClient;

namespace kindyun {

/**
 * @brief 会话元信息
 */
struct SessionInfo {
    std::string id;                  // UUID v4
    std::string created_at;          // ISO 8601
    std::string last_active_at;
    int msg_count = 0;
    std::string system_prompt;
    std::string source;              // "cli" | "web" | "auto"（创建来源）
    int approval_mode = 0;           // 0=interactive, 1=auto_approve, 2=auto_deny
};

/**
 * @brief 会话对象（含元信息 + LLMClient + ApprovalManager + ConversationLoop）
 *
 * 字段顺序保证 client 先于 loop 析构（C++ 按声明逆序析构）。
 * ApprovalManager 与 loop 同寿命。
 */
struct SessionEntry {
    SessionInfo info;
    std::unique_ptr<LLMClient> client;
    std::unique_ptr<ApprovalManager> approval;
    std::unique_ptr<ConversationLoop> loop;
};

/**
 * @class SessionManager
 * @brief 多会话管理器（单例）
 */
class SessionManager {
public:
    static SessionManager& instance();

    /**
     * @brief 初始化（启动时调用一次）
     *
     * 注：ApprovalManager 不再共享，每个会话会自建一个。approval_required
     *     列表传给每个会话的 ApprovalManager 实例，mode 在 createSession 时决定。
     */
    void init(const std::string& llm_url,
              const std::string& llm_model,
              int curl_timeout,
              int stream_timeout,
              class ToolDispatcher& dispatcher,
              const class Config& config,
              const std::vector<std::string>& approval_required,
              class MemoryDB* memdb,
              int max_concurrent);

    /**
     * @brief 创建新会话
     *
     * @param system_prompt 自定义系统提示（空则用全局默认）
     * @param source        创建来源："cli" | "web" | "auto"
     * @param approval_mode ApprovalManager::Mode 值
     *                      - interactive: CLI 模式（等待 stdin）
     *                      - auto_approve: Web 模式（自动放行）
     *                      - auto_deny: 全部拒绝
     * @return 新会话 ID；失败返回空串（达到 max_concurrent 时）
     */
    std::string createSession(const std::string& system_prompt = "",
                              const std::string& source = "auto",
                              int approval_mode = 0 /* interactive */);

    /**
     * @brief 获取会话（取出 ConversationLoop 指针）
     *
     * 顺带刷新 last_active_at。
     * 返回的指针仅在持有 SessionManager 引用期间有效（不要 delete）。
     */
    class ConversationLoop* get(const std::string& sid);

    /**
     * @brief 销毁会话
     */
    bool destroy(const std::string& sid);

    /**
     * @brief 列出会话（仅元信息）
     */
    std::vector<SessionInfo> list();

    /**
     * @brief 清空某会话历史
     */
    bool clearHistory(const std::string& sid);

    /**
     * @brief 当前会话数
     */
    int size() const;

    /**
     * @brief 获取会话消息历史（只读快照）
     */
    json snapshotHistory(const std::string& sid);

private:
    SessionManager() = default;

    /**
     * @brief 内部：构造一个 SessionEntry（client + approval + loop）
     */
    SessionEntry makeEntry(const std::string& system_prompt, int approval_mode);

    /**
     * @brief 生成 UUID v4（基于随机数 + 计数器）
     */
    static std::string makeUuid();

    static std::string nowIso8601();

private:
    // 共享依赖（init 时填充）
    std::string m_llm_url;
    std::string m_llm_model;
    int m_curl_timeout = 120;
    int m_stream_timeout = 300;
    class ToolDispatcher* m_dispatcher = nullptr;
    const class Config* m_config = nullptr;
    std::vector<std::string> m_approval_required;
    class MemoryDB* m_memdb = nullptr;
    int m_max_concurrent = 64;

    // 会话存储
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, SessionEntry> m_sessions;

    // UUID 计数器
    static std::atomic<uint64_t> s_uuid_counter;
};

} // namespace kindyun

#endif // KINDYUN_SESSION_MANAGER_HPP