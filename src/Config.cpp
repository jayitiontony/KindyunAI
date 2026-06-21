/**
 * @file Config.cpp
 * @brief 配置管理器实现
 *
 * 实现 Config 单例和所有配置访问方法。
 * 使用嵌套键访问（如 "llm.model"）访问深层配置值。
 */

#include "kindyun/Config.hpp"
#include <fstream>
#include <iostream>

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Config] Warning: Config file not found,Use default: " << path << std::endl;
        return false;
    }
    try {
        f >> m_config;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Config] Parase config file failed: " << e.what() << std::endl;
        return false;
    }
}

std::string Config::getStr(const std::string& key, const std::string& fallback) const {
    if (m_config.contains(key)) return m_config[key].get<std::string>();
    return fallback;
}

int Config::getInt(const std::string& key, int fallback) const {
    if (m_config.contains(key)) return m_config[key].get<int>();
    return fallback;
}

bool Config::getBool(const std::string& key, bool fallback) const {
    if (m_config.contains(key)) return m_config[key].get<bool>();
    return fallback;
}

json Config::getJson(const std::string& key) const {
    if (m_config.contains(key)) return m_config[key];
    return nullptr;
}

// ===== LLM 配置 =====
std::string Config::llm_url() const {
    if (m_config.contains("llm") && m_config["llm"].contains("base_url"))
        return m_config["llm"]["base_url"].get<std::string>();
    return "http://localhost:8080";
}
std::string Config::llm_model() const {
    if (m_config.contains("llm") && m_config["llm"].contains("model"))
        return m_config["llm"]["model"].get<std::string>();
    return "llama3";
}
int Config::llm_max_tokens() const {
    if (m_config.contains("llm") && m_config["llm"].contains("max_tokens"))
        return m_config["llm"]["max_tokens"].get<int>();
    return 4096;
}
double Config::llm_temperature() const {
    if (m_config.contains("llm") && m_config["llm"].contains("temperature"))
        return m_config["llm"]["temperature"].get<double>();
    return 0.7;
}
bool Config::debug_conversation() const {
    if (m_config.contains("llm") && m_config["llm"].contains("debug_conversation"))
        return m_config["llm"]["debug_conversation"].get<bool>();
    return false;
}

// ===== 上下文压缩配置（Phase 3/滑动窗口）=====
size_t Config::max_context_tokens() const {
    if (m_config.contains("llm") && m_config["llm"].contains("max_context_tokens"))
        return static_cast<size_t>(m_config["llm"]["max_context_tokens"].get<int>());
    return 4096; // 默认 4K tokens
}
double Config::compress_threshold_ratio() const {
    if (m_config.contains("llm") && m_config["llm"].contains("compress_threshold_ratio"))
        return m_config["llm"]["compress_threshold_ratio"].get<double>();
    return 0.8; // 默认80% 阈值
}
int Config::preserve_recent_messages() const {
    if (m_config.contains("llm") && m_config["llm"].contains("preserve_recent_messages"))
        return m_config["llm"]["preserve_recent_messages"].get<int>();
    return 10; // 默认保留最近 10 条
}
size_t Config::sliding_window_tokens() const {
    if (m_config.contains("llm") && m_config["llm"].contains("sliding_window_tokens"))
        return static_cast<size_t>(m_config["llm"]["sliding_window_tokens"].get<int>());
    return 2048; // 默认目标 2K tokens
}
bool Config::enable_summarization() const {
    if (m_config.contains("llm") && m_config["llm"].contains("enable_summarization"))
        return m_config["llm"]["enable_summarization"].get<bool>();
    return true; // 默认启用 LLM 摘要
}

// ===== Agent 行为配置 =====
int Config::max_iterations() const {
    if (m_config.contains("agent") && m_config["agent"].contains("max_iterations"))
        return m_config["agent"]["max_iterations"].get<int>();
    return 50;
}
int Config::empty_response_retries() const {
    if (m_config.contains("agent") && m_config["agent"].contains("empty_response_retries"))
        return m_config["agent"]["empty_response_retries"].get<int>();
    return 3;
}

// ===== 工具集配置 =====
std::vector<std::string> Config::enabled_toolsets() const {
    if (m_config.contains("tools") && m_config["tools"].contains("enabled_toolsets"))
        return m_config["tools"]["enabled_toolsets"].get<std::vector<std::string>>();
    return {"core", "terminal", "edit", "search", "memory", "todo"};
}
std::vector<std::string> Config::disabled_tools() const {
    if (m_config.contains("tools") && m_config["tools"].contains("disabled_tools"))
        return m_config["tools"]["disabled_tools"].get<std::vector<std::string>>();
    return {};
}

// ===== 安全配置 =====
size_t Config::max_file_read_chars() const {
    if (m_config.contains("security") && m_config["security"].contains("max_file_read_chars"))
        return static_cast<size_t>(m_config["security"]["max_file_read_chars"].get<int>());
    return 100000;
}
int Config::terminal_timeout_seconds() const {
    if (m_config.contains("security") && m_config["security"].contains("terminal_timeout_seconds"))
        return m_config["security"]["terminal_timeout_seconds"].get<int>();
    return 300;
}

std::vector<std::string> Config::protected_paths() const {
    if (m_config.contains("security") && m_config["security"].contains("protected_paths")) {
        return m_config["security"]["protected_paths"].get<std::vector<std::string>>();
    }
    return {"C:\\Windows", "C:\\Windows\\System32", "/etc"};
}

// ===== HTTP 超时配置 =====
int Config::http_curl_timeout() const {
    if (m_config.contains("http") && m_config["http"].contains("curl_timeout_seconds"))
        return m_config["http"]["curl_timeout_seconds"].get<int>();
    return 120; // 默认 120 秒
}
int Config::http_stream_timeout() const {
    if (m_config.contains("http") && m_config["http"].contains("stream_timeout_seconds"))
        return m_config["http"]["stream_timeout_seconds"].get<int>();
    return 300; // 默认 300 秒
}

std::vector<std::string> Config::getApprovalRequired() const {
    if (m_config.contains("tools") && m_config["tools"].contains("approval_required")) {
        return m_config["tools"]["approval_required"].get<std::vector<std::string>>();
    }
    return {"write_file", "remove_file", "terminal", "patch"};
}

// ===== Web 服务配置 =====
namespace {
    inline const json& web_node(const json& root) {
        static json empty = json::object();
        auto it = root.find("web");
        if (it == root.end() || !it->is_object()) return empty;
        return *it;
    }
}

bool Config::web_enabled() const {
    const auto& w = web_node(m_config);
    if (w.contains("enabled")) return w["enabled"].get<bool>();
    return true;
}

std::string Config::web_host() const {
    const auto& w = web_node(m_config);
    if (w.contains("host")) return w["host"].get<std::string>();
    return "127.0.0.1";
}

int Config::web_port() const {
    const auto& w = web_node(m_config);
    if (w.contains("port")) return w["port"].get<int>();
    return 8765;
}

std::string Config::web_api_token() const {
    const auto& w = web_node(m_config);
    if (w.contains("api_token")) return w["api_token"].get<std::string>();
    return "";
}

bool Config::web_enable_external_api() const {
    const auto& w = web_node(m_config);
    if (w.contains("enable_external_api")) return w["enable_external_api"].get<bool>();
    return true;
}

std::vector<std::string> Config::web_cors_allow_origins() const {
    const auto& w = web_node(m_config);
    if (w.contains("cors_allow_origins")) {
        return w["cors_allow_origins"].get<std::vector<std::string>>();
    }
    return {"*"};
}

std::string Config::web_static_dir() const {
    const auto& w = web_node(m_config);
    if (w.contains("static_dir")) return w["static_dir"].get<std::string>();
    return "web/static";
}

std::string Config::web_templates_dir() const {
    const auto& w = web_node(m_config);
    if (w.contains("templates_dir")) return w["templates_dir"].get<std::string>();
    return "web/templates";
}

int Config::web_rate_limit_rpm() const {
    const auto& w = web_node(m_config);
    if (w.contains("rate_limit") && w["rate_limit"].contains("requests_per_minute")) {
        return w["rate_limit"]["requests_per_minute"].get<int>();
    }
    return 120;
}

int Config::web_session_idle_ttl_seconds() const {
    const auto& w = web_node(m_config);
    if (w.contains("session") && w["session"].contains("idle_ttl_seconds")) {
        return w["session"]["idle_ttl_seconds"].get<int>();
    }
    return 3600;
}

int Config::web_session_max_concurrent() const {
    const auto& w = web_node(m_config);
    if (w.contains("session") && w["session"].contains("max_concurrent")) {
        return w["session"]["max_concurrent"].get<int>();
    }
    return 64;
}

bool Config::web_session_persist_on_disk() const {
    const auto& w = web_node(m_config);
    if (w.contains("session") && w["session"].contains("persist_on_disk")) {
        return w["session"]["persist_on_disk"].get<bool>();
    }
    return false;
}

std::string Config::web_session_db_path() const {
    const auto& w = web_node(m_config);
    if (w.contains("session") && w["session"].contains("db_path")) {
        return w["session"]["db_path"].get<std::string>();
    }
    return "kindyun_memory_server.db";
}

std::string Config::web_approval_mode() const {
    const auto& w = web_node(m_config);
    if (w.contains("approval_mode")) return w["approval_mode"].get<std::string>();
    return "auto";
}

std::string Config::web_log_level() const {
    const auto& w = web_node(m_config);
    if (w.contains("log") && w["log"].contains("level")) {
        return w["log"]["level"].get<std::string>();
    }
    return "info";
}

bool Config::web_access_log() const {
    const auto& w = web_node(m_config);
    if (w.contains("log") && w["log"].contains("access_log")) {
        return w["log"]["access_log"].get<bool>();
    }
    return true;
}

json Config::web_external_services_config() const {
    const auto& w = web_node(m_config);
    if (w.contains("external_services")) return w["external_services"];
    return json::object();
}
