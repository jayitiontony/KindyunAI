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
 * @file ApiHandlers.cpp
 * @brief REST + UI 路由实现
 */

#include "ApiHandlers.hpp"
#include "kindyun/Config.hpp"
#include "kindyun/ConversationLoop.hpp"
#include "kindyun/ToolBase.hpp"
#include "kindyun/ToolDispatcher.hpp"
#include "kindyun/MemoryDB.hpp"
#include "kindyun/Types.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <thread>

namespace kindyun {

namespace {

// 单次启动时间戳（用于 uptime）
const auto g_start_time = std::chrono::steady_clock::now();

// 服务器版本
constexpr const char* kServerVersion = "1.0.0";

// ============================================================
// 辅助
// ============================================================

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

std::string guessContentType(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css")  return "text/css; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js")   return "application/javascript; charset=utf-8";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

long long uptimeSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - g_start_time).count();
}

// 简易 URL 路径安全检查（防 ../ 穿越）
bool isSafeRelative(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.find("..") != std::string::npos) return false;
    return true;
}

// ============================================================
// Health / Version
// ============================================================

json healthHandler(const RequestCtx&, SessionManager& sm) {
    return {
        {"ok", true},
        {"version", kServerVersion},
        {"uptime_sec", uptimeSec()},
        {"sessions", sm.size()}
    };
}

json versionHandler(const RequestCtx&) {
    return {
        {"server", "KindyunAIServer"},
        {"version", kServerVersion},
        {"protocol", "v1"}
    };
}

// ============================================================
// Sessions
// ============================================================

json createSessionHandler(const RequestCtx& ctx, SessionManager& sm) {
    json body = ctx.jsonBody();
    std::string sys = body.value("system_prompt", std::string(""));
    // Web 端默认 auto_approve（用户在浏览器没法交互 stdin 审批）
    std::string sid = sm.createSession(sys, "web", 1 /* auto_approve */);
    if (sid.empty()) {
        json out = {{"_status", 503}, {"error", {{"code", "max_concurrent"},
                    {"message", "max concurrent sessions reached"}}}};
        return out;
    }
    json out = {{"_status", 201}, {"session_id", sid}};
    return out;
}

json listSessionsHandler(const RequestCtx&, SessionManager& sm) {
    json arr = json::array();
    for (auto& info : sm.list()) {
        arr.push_back({
            {"id", info.id},
            {"created_at", info.created_at},
            {"last_active_at", info.last_active_at},
            {"msg_count", info.msg_count}
        });
    }
    return {{"sessions", arr}, {"count", arr.size()}};
}

json getSessionHandler(const RequestCtx& ctx, SessionManager& sm) {
    std::string sid = ctx.pathParam("id");
    if (!sm.get(sid)) {
        json out = {{"_status", 404},
                    {"error", {{"code", "session_not_found"},
                              {"message", "session_id does not exist"}}}};
        return out;
    }
    json history = sm.snapshotHistory(sid);
    return {
        {"session_id", sid},
        {"message_count", history.size()},
        {"history", history}
    };
}

json deleteSessionHandler(const RequestCtx& ctx, SessionManager& sm) {
    std::string sid = ctx.pathParam("id");
    if (!sm.destroy(sid)) {
        json out = {{"_status", 404},
                    {"error", {{"code", "session_not_found"}, {"message", "no such session"}}}};
        return out;
    }
    return {{"ok", true}, {"session_id", sid}};
}

json clearSessionHandler(const RequestCtx& ctx, SessionManager& sm) {
    std::string sid = ctx.pathParam("id");
    if (!sm.clearHistory(sid)) {
        json out = {{"_status", 404},
                    {"error", {{"code", "session_not_found"}, {"message", "no such session"}}}};
        return out;
    }
    return {{"ok", true}, {"session_id", sid}};
}

// ============================================================
// Chat（同步）
// ============================================================

json chatHandler(const RequestCtx& ctx, SessionManager& sm) {
    json body = ctx.jsonBody();
    std::string msg = body.value("message", std::string(""));
    std::string sid = body.value("session_id", std::string(""));

    if (msg.empty()) {
        json out = {{"_status", 400},
                    {"error", {{"code", "validation_error"},
                              {"message", "message is required"}}}};
        return out;
    }

    // 自动建会话（无 sid 时）—— web 模式用 auto_approve
    if (sid.empty()) {
        sid = sm.createSession("", "web", 1 /* auto_approve */);
        if (sid.empty()) {
            json out = {{"_status", 503},
                        {"error", {{"code", "max_concurrent"},
                                  {"message", "max concurrent sessions reached"}}}};
            return out;
        }
    }

    auto* loop = sm.get(sid);
    if (!loop) {
        json out = {{"_status", 404},
                    {"error", {{"code", "session_not_found"},
                              {"message", "invalid session_id"}}}};
        return out;
    }

    try {
        std::string reply = loop->run(msg);
        return {
            {"session_id", sid},
            {"reply", reply},
            {"tool_calls", json::array()}  // TODO: 暴露 tool_calls
        };
    } catch (const std::exception& e) {
        json out = {{"_status", 502},
                    {"error", {{"code", "llm_error"}, {"message", e.what()}}}};
        return out;
    }
}

// ============================================================
// Chat（流式 SSE）
// ============================================================

void chatStreamHandler(const httplib::Request& req, httplib::Response& res,
                       SessionManager& sm) {
    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        HttpServer::writeError(res, 400, "validation_error", "body must be JSON");
        return;
    }

    std::string msg = body.value("message", std::string(""));
    std::string sid = body.value("session_id", std::string(""));

    if (msg.empty()) {
        HttpServer::writeError(res, 400, "validation_error", "message is required");
        return;
    }

    if (sid.empty()) sid = sm.createSession("", "web", 1 /* auto_approve */);
    if (sid.empty()) {
        HttpServer::writeError(res, 503, "max_concurrent", "max concurrent sessions reached");
        return;
    }

    auto* loop = sm.get(sid);
    if (!loop) {
        HttpServer::writeError(res, 404, "session_not_found", "invalid session_id");
        return;
    }

    // SSE 响应头
    HttpServer::setSSEHeaders(res);

    // chunked provider：循环里把每段写成 SSE 帧
    auto provider = [loop, msg, sid](size_t /*offset*/, httplib::DataSink& sink) -> bool {
        try {
            // 先写 meta
            std::string meta = "event: meta\ndata: "
                + HttpServer::toJsonString({{"session_id", sid}}) + "\n\n";
            sink.write(meta.data(), meta.size());

            // 调用 runStream（新 StreamEvent 事件流）
            std::string full;
            loop->runStream(msg, [&sink, &full](const StreamEvent& ev) -> bool {
                std::string frame;
                switch (ev.type) {
                    case StreamEvent::Text: {
                        if (ev.text.empty()) return true;
                        full += ev.text;
                        frame = "event: delta\ndata: "
                              + HttpServer::toJsonString({{"text", ev.text}}) + "\n\n";
                        break;
                    }
                    case StreamEvent::ToolPending: {
                        frame = "event: tool\ndata: "
                              + HttpServer::toJsonString({
                                  {"id", ev.tool_call_id},
                                  {"name", ev.tool_name},
                                  {"arguments", ev.tool_args}
                              }) + "\n\n";
                        break;
                    }
                    case StreamEvent::ToolResult: {
                        // 截断过长的结果（前端显示友好）
                        std::string preview = ev.tool_result;
                        if (preview.size() > 500) {
                            preview = preview.substr(0, 500) + "... (truncated)";
                        }
                        frame = "event: tool_result\ndata: "
                              + HttpServer::toJsonString({
                                  {"id", ev.tool_call_id},
                                  {"name", ev.tool_name},
                                  {"result", preview},
                                  {"is_error", ev.is_error}
                              }) + "\n\n";
                        break;
                    }
                    case StreamEvent::Done:
                        // 跳过 —— 我们自己发 done 事件
                        return true;
                }
                if (!frame.empty()) {
                    sink.write(frame.data(), frame.size());
                }
                return true; // 继续接收
            });

            // done
            json done = {{"reply", full}, {"ok", true}, {"elapsed_ms", 0}};
            std::string done_frame = "event: done\ndata: "
                + HttpServer::toJsonString(done) + "\n\n";
            sink.write(done_frame.data(), done_frame.size());
            sink.done();
            return true;
        } catch (const std::exception& e) {
            std::string err = "event: error\ndata: "
                + HttpServer::toJsonString({{"message", e.what()}}) + "\n\n";
            sink.write(err.data(), err.size());
            sink.done();
            return true;
        }
    };

    res.set_chunked_content_provider("text/event-stream", provider);
}

// ============================================================
// Tools
// ============================================================

json listToolsHandler(const RequestCtx&) {
    auto defs = ToolRegistry::instance().getToolDefinitions();
    json arr = json::array();
    for (auto& d : defs) {
        arr.push_back({
            {"name", d.name},
            {"description", d.description},
            {"parameters", d.parameters},
            {"toolset", d.toolset},
            {"requires_approval", d.requires_approval}
        });
    }
    return {{"tools", arr}, {"count", arr.size()}};
}

json invokeToolHandler(const RequestCtx& ctx) {
    std::string name = ctx.pathParam("name");
    json body = ctx.jsonBody();
    json args = body.value("arguments", json::object());

    if (!ToolRegistry::instance().hasTool(name)) {
        json out = {{"_status", 404},
                    {"error", {{"code", "tool_not_found"},
                              {"message", "tool '" + name + "' not registered"}}}};
        return out;
    }

    std::string err;
    std::string result = ToolRegistry::instance().executeTool(name, args, &err);
    bool is_error = !err.empty();

    return {
        {"tool", name},
        {"result", result},
        {"is_error", is_error},
        {"error", is_error ? json(err) : nullptr}
    };
}

// ============================================================
// Memory
// ============================================================

json listMemoryHandler(const RequestCtx&) {
    // 复用 MemoryDB 单例（无注册接口的简化方案：从 ToolRegistry 的 m_memory_db 拿不到）
    // 这里走 tool: memory_tool 的等价接口
    // 简化：调 ToolRegistry::instance().memoryDB() 私有不可见，改用全局 MemoryDB 单例
    // —— 暂未实现 MemoryDB 单例；返回空 + TODO
    json out = {{"_status", 501},
                {"error", {{"code", "not_implemented"},
                          {"message", "memory listing not yet wired; use /api/v1/tools/memory_tool/invoke"}}}};
    return out;
}

json searchMemoryHandler(const RequestCtx&) {
    json out = {{"_status", 501},
                {"error", {{"code", "not_implemented"},
                          {"message", "memory search not yet wired; use /api/v1/tools/session_search/invoke"}}}};
    return out;
}

// ============================================================
// External Services
// ============================================================

json externalCallHandler(const RequestCtx& ctx,
                         ExternalServiceRegistry& reg,
                         const ::Config& cfg) {
    std::string svc_name = ctx.pathParam("service");
    json body = ctx.jsonBody();
    std::string action = body.value("action", std::string(""));
    json params = body.value("params", json::object());
    std::string auth_override = body.value("auth_token", std::string(""));

    if (action.empty()) {
        json out = {{"_status", 400},
                    {"error", {{"code", "validation_error"},
                              {"message", "action is required"}}}};
        return out;
    }

    // 启用检查
    if (!cfg.web_enable_external_api()) {
        json out = {{"_status", 403},
                    {"error", {{"code", "external_api_disabled"},
                              {"message", "web.enable_external_api = false"}}}};
        return out;
    }

    // 白名单/黑名单
    auto allow = cfg.web_cors_allow_origins();  // 复用 web_cors_allow_origins 不对；改用 web_external_services_config
    (void)allow;
    auto ext_cfg = cfg.web_external_services_config();
    std::vector<std::string> allowlist, denylist;
    if (ext_cfg.contains("allowlist") && ext_cfg["allowlist"].is_array()) {
        for (auto& v : ext_cfg["allowlist"]) allowlist.push_back(v.get<std::string>());
    }
    if (ext_cfg.contains("denylist") && ext_cfg["denylist"].is_array()) {
        for (auto& v : ext_cfg["denylist"]) denylist.push_back(v.get<std::string>());
    }
    if (!reg.isAllowed(svc_name, allowlist, denylist)) {
        json out = {{"_status", 403},
                    {"error", {{"code", "service_not_allowed"},
                              {"message", "service '" + svc_name + "' not in allowlist or in denylist"}}}};
        return out;
    }

    auto svc = reg.get(svc_name);
    if (!svc) {
        json out = {{"_status", 404},
                    {"error", {{"code", "service_not_found"},
                              {"message", "unknown external service: " + svc_name}}}};
        return out;
    }

    try {
        auto resp = svc->call(action, params, auth_override);
        json out = {
            {"service", svc_name},
            {"action", action},
            {"http_status", resp.http_status},
            {"body", resp.body},
            {"elapsed_ms", resp.elapsed_ms}
        };
        if (resp.transport_error) {
            out["transport_error"] = true;
            out["error"] = resp.error_message;
            out["_status"] = 502;
        }
        if (resp.http_status >= 400) out["_status"] = resp.http_status;
        return out;
    } catch (const std::exception& e) {
        json out = {{"_status", 500},
                    {"error", {{"code", "service_error"}, {"message", e.what()}}}};
        return out;
    }
}

json listServicesHandler(const RequestCtx&, ExternalServiceRegistry& reg) {
    json arr = json::array();
    for (auto& name : reg.listServices()) {
        auto svc = reg.get(name);
        if (!svc) continue;
        arr.push_back({
            {"name", svc->name()},
            {"base_url", svc->baseUrl()},
            {"description", svc->description()},
            {"supported_actions", svc->supportedActions()}
        });
    }
    return {{"services", arr}, {"count", arr.size()}};
}

// ============================================================
// UI（静态资源 + 主页）
// ============================================================

void indexHandler(const httplib::Request&, httplib::Response& res, const std::string& tpl_dir) {
    std::string path = tpl_dir + "/index.html";
    std::string html = readFile(path);
    if (html.empty()) {
        res.status = 500;
        res.set_content("<h1>KindyunAI</h1><p>index.html not found at " + path + "</p>",
                        "text/html; charset=utf-8");
        return;
    }
    res.set_content(html, "text/html; charset=utf-8");
}

void staticHandler(const httplib::Request& req, httplib::Response& res, const std::string& static_dir) {
    std::string rel = req.path.substr(8); // strip "/static/"
    if (!isSafeRelative(rel)) {
        res.status = 400;
        res.set_content("bad path", "text/plain");
        return;
    }
    std::string full = static_dir + "/" + rel;
    std::string content = readFile(full);
    if (content.empty()) {
        res.status = 404;
        res.set_content("not found", "text/plain");
        return;
    }
    res.set_content(content, guessContentType(full));
}

} // namespace

// ============================================================
// 入口：注册全部路由
// ============================================================

void registerApiHandlers(HttpServer& server,
                         SessionManager& sm,
                         ExternalServiceRegistry& ext_registry,
                         const Config& cfg) {

    // ----- 健康/版本 -----
    server.Get("/api/v1/health",   [&sm](const RequestCtx& c){ return healthHandler(c, sm); });
    server.Get("/api/v1/healthz",  [&sm](const RequestCtx& c){ return healthHandler(c, sm); });
    server.Get("/api/v1/version",  [](const RequestCtx& c){ return versionHandler(c); });

    // ----- 会话 -----
    server.Post("/api/v1/sessions",
        [&sm](const RequestCtx& c){ return createSessionHandler(c, sm); });
    server.Get("/api/v1/sessions",
        [&sm](const RequestCtx& c){ return listSessionsHandler(c, sm); });
    server.Get("/api/v1/sessions/:id",
        [&sm](const RequestCtx& c){ return getSessionHandler(c, sm); });
    server.Delete("/api/v1/sessions/:id",
        [&sm](const RequestCtx& c){ return deleteSessionHandler(c, sm); });
    server.Post("/api/v1/sessions/:id/clear",
        [&sm](const RequestCtx& c){ return clearSessionHandler(c, sm); });

    // ----- 对话 -----
    server.Post("/api/v1/chat",
        [&sm](const RequestCtx& c){ return chatHandler(c, sm); });

    // 流式（SSE）—— 走原始 handler（chunked provider）
    server.Post("/api/v1/chat/stream",
        [&sm](const httplib::Request& req, httplib::Response& res) {
            chatStreamHandler(req, res, sm);
        });

    // ----- 工具 -----
    server.Get("/api/v1/tools",
        [](const RequestCtx& c){ return listToolsHandler(c); });
    server.Post("/api/v1/tools/:name/invoke",
        [](const RequestCtx& c){ return invokeToolHandler(c); });

    // ----- 记忆 -----
    server.Get("/api/v1/memory",
        [](const RequestCtx& c){ return listMemoryHandler(c); });
    server.Get("/api/v1/memory/search",
        [](const RequestCtx& c){ return searchMemoryHandler(c); });

    // ----- 外部服务 -----
    server.Get("/api/v1/external",
        [&ext_registry](const RequestCtx& c){ return listServicesHandler(c, ext_registry); });
    server.Post("/api/v1/external/:service",
        [&ext_registry, &cfg](const RequestCtx& c){
            return externalCallHandler(c, ext_registry, cfg);
        });

    // ----- UI -----
    std::string tpl_dir = cfg.web_templates_dir();
    std::string static_dir = cfg.web_static_dir();

    server.Get("/", [tpl_dir](const httplib::Request& req, httplib::Response& res) {
        indexHandler(req, res, tpl_dir);
    });
    // /static/ 用正则路由（cpp-httplib 的 * 通配行为与 path_params 冲突，用正则更明确）
    server.Get(R"(/static/(.*))", [static_dir](const httplib::Request& req, httplib::Response& res) {
        staticHandler(req, res, static_dir);
    });
}

} // namespace kindyun