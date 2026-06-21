/**
 * @file MemoryDB.hpp
 * @brief SQLite 持久化记忆系统
 *
 * Phase 2 实现：
 *   - 使用 SQLite（sqlite3.c amalgamation）持久化存储
 *   - 两张表：memory（键值对）+ session_history（会话历史）
 *   - 支持 save/search/list/clear 操作
 *
 * 表结构：
 *   CREATE TABLE memory (
 *     key TEXT PRIMARY KEY,
 *     value TEXT,
 *     created_at INTEGER,
 *     updated_at INTEGER
 *   );
 *
 *   CREATE TABLE session_history (
 *     id INTEGER PRIMARY KEY AUTOINCREMENT,
 *     role TEXT,
 *     content TEXT,
 *     tool_call_id TEXT,
 *     created_at INTEGER
 *   );
 */

#pragma once
#include <string>
#include <vector>
#include "sqlite3.h"

struct MemoryEntry {
    std::string key;
    std::string value;
    long long created_at;
    long long updated_at;
};

class MemoryDB {
public:
    /// 构造并打开数据库（文件不存在则创建）
    explicit MemoryDB(const std::string& db_path);
    ~MemoryDB();

    /// 是否成功打开
    bool isOpen() const;

    /// 获取错误信息
    const char* lastError() const;

    // ===== memory 表操作 =====
    /// 保存键值对（upsert）
    bool save(const std::string& key, const std::string& value);
    /// 按 key 查询
    bool get(const std::string& key, std::string& out_value) const;
    /// 按 key prefix模糊搜索
    std::vector<MemoryEntry> search(const std::string& query) const;
    /// 列出所有记忆
    std::vector<MemoryEntry> listAll() const;
    /// 删除指定 key
    bool remove(const std::string& key);
    /// 清空所有记忆
    bool clearAll();

    // ===== session_history 表操作 =====
    /// 添加会话消息
    bool addMessage(const std::string& role,
                    const std::string& content,
                    const std::string& tool_call_id = "");
    /// 搜索会话历史
    std::vector<std::string> searchHistory(const std::string& query) const;
    /// 获取最近 N 条会话
    std::vector<std::string> getRecentMessages(int limit = 20) const;
    /// 清空会话历史
    bool clearHistory();

private:
    bool initSchema();
    long long now() const;

    sqlite3* m_db = nullptr;
    std::string m_db_path;
    std::string m_last_error;
    bool m_open = false;
};