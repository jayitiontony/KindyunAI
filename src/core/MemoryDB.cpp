/**
 * @file MemoryDB.cpp
 * @brief SQLite 持久化记忆系统实现
 */

#include "kindyun/MemoryDB.hpp"
#include <iostream>
#include <ctime>
#include <algorithm>

// ============================================================================
// 内部工具
// ============================================================================
static long long timestamp_now() {
    return (long long)time(nullptr);
}

// ============================================================================
// 构造 / 析构
// ============================================================================
MemoryDB::MemoryDB(const std::string& db_path) : m_db_path(db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        m_last_error = m_db ? sqlite3_errmsg(m_db) : "open failed";
        m_db = nullptr;
        return;
    }
    m_open = true;
    if (!initSchema()) {
        m_last_error = "schema init failed";
    }
}

MemoryDB::~MemoryDB() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool MemoryDB::isOpen() const { return m_open; }
const char* MemoryDB::lastError() const { return m_last_error.c_str(); }
long long MemoryDB::now() const { return timestamp_now(); }

// ============================================================================
// 初始化表结构
// ============================================================================
bool MemoryDB::initSchema() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS memory ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT,"
        "  created_at INTEGER,"
        "  updated_at INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS session_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  role TEXT,"
        "  content TEXT,"
        "  tool_call_id TEXT,"
        "  created_at INTEGER"
        ");";

    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            m_last_error = err;
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

// ============================================================================
// memory 表操作
// ============================================================================
bool MemoryDB::save(const std::string& key, const std::string& value) {
    if (!m_db) return false;

    const char* sql =
        "INSERT INTO memory (key, value, created_at, updated_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value,"
        "updated_at=excluded.updated_at";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        m_last_error = sqlite3_errmsg(m_db);
        return false;
    }

    long long t = now();
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, t);
    sqlite3_bind_int64(stmt, 4, t);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        m_last_error = sqlite3_errmsg(m_db);
        return false;
    }
    return true;
}

bool MemoryDB::get(const std::string& key, std::string& out_value) const {
    if (!m_db) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT value FROM memory WHERE key=?", -1, &stmt, nullptr)
        != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* t = (const char*)sqlite3_column_text(stmt, 0);
        out_value = t ? t : "";
        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}

std::vector<MemoryEntry> MemoryDB::search(const std::string& query) const {
    std::vector<MemoryEntry> result;
    if (!m_db || query.empty()) return result;

    sqlite3_stmt* stmt = nullptr;
    std::string like = "%" + query + "%";
    if (sqlite3_prepare_v2(m_db,
        "SELECT key, value, created_at, updated_at FROM memory "
        "WHERE key LIKE ? OR value LIKE ? "
        "ORDER BY updated_at DESC LIMIT 50",
        -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, like.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        const char* k = (const char*)sqlite3_column_text(stmt, 0);
        const char* v = (const char*)sqlite3_column_text(stmt, 1);
        e.key = k ? k : "";
        e.value = v ? v : "";
        e.created_at = sqlite3_column_int64(stmt, 2);
        e.updated_at = sqlite3_column_int64(stmt, 3);
        result.push_back(e);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<MemoryEntry> MemoryDB::listAll() const {
    std::vector<MemoryEntry> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT key, value, created_at, updated_at FROM memory "
        "ORDER BY updated_at DESC LIMIT 100",
        -1, &stmt, nullptr) != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        const char* k = (const char*)sqlite3_column_text(stmt, 0);
        const char* v = (const char*)sqlite3_column_text(stmt, 1);
        e.key = k ? k : "";
        e.value = v ? v : "";
        e.created_at = sqlite3_column_int64(stmt, 2);
        e.updated_at = sqlite3_column_int64(stmt, 3);
        result.push_back(e);
    }

    sqlite3_finalize(stmt);
    return result;
}

bool MemoryDB::remove(const std::string& key) {
    if (!m_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "DELETE FROM memory WHERE key=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

bool MemoryDB::clearAll() {
    if (!m_db) return false;
    return sqlite3_exec(m_db, "DELETE FROM memory", nullptr, nullptr, nullptr)
           == SQLITE_OK;
}

// ============================================================================
// session_history 表操作
// ============================================================================
bool MemoryDB::addMessage(const std::string& role,
                           const std::string& content,
                           const std::string& tool_call_id) {
    if (!m_db) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "INSERT INTO session_history (role, content, tool_call_id, created_at) "
        "VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, tool_call_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, now());

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}

std::vector<std::string> MemoryDB::searchHistory(const std::string& query) const {
    std::vector<std::string> result;
    if (!m_db || query.empty()) return result;

    sqlite3_stmt* stmt = nullptr;
    std::string like = "%" + query + "%";
    if (sqlite3_prepare_v2(m_db,
        "SELECT role, content FROM session_history "
        "WHERE content LIKE ? ORDER BY id DESC LIMIT 20",
        -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* role = (const char*)sqlite3_column_text(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        std::string entry = std::string(role ? role : "") + ": "
                           + (content ? content : "");
        result.push_back(entry);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> MemoryDB::getRecentMessages(int limit) const {
    std::vector<std::string> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
        "SELECT role, content FROM session_history "
        "ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_int64(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* role = (const char*)sqlite3_column_text(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        result.push_back(std::string(role ? role : "") + ": " +
                        (content ? content : ""));
    }

    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

bool MemoryDB::clearHistory() {
    if (!m_db) return false;
    return sqlite3_exec(m_db, "DELETE FROM session_history",
                       nullptr, nullptr, nullptr) == SQLITE_OK;
}