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
 * @file HttpServer.cpp
 * @brief HttpServer 实现
 */

#include "kindyun/HttpServer.hpp"

#include <iostream>
#include <utility>

namespace kindyun {

// ===== 构造 / 析构 =====

HttpServer::HttpServer(const std::string& host, int port)
    : m_host(host), m_port(port), m_server(std::make_unique<httplib::Server>()) {
    // 合理超时设置
    m_server->set_read_timeout(60, 0);
    m_server->set_write_timeout(60, 0);
    m_server->set_payload_max_length(16 * 1024 * 1024);  // 16 MiB
    m_server->set_keep_alive_max_count(20);

    // 默认异常处理：避免某个 handler 抛异常导致进程崩溃
    m_server->set_exception_handler(
        [](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string what = "unknown";
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) {}
            writeError(res, 500, "internal_error", what);
        });
}

HttpServer::~HttpServer() {
    if (m_server) {
        m_server->stop();
    }
}

// ===== 路由注册（JSON 便捷接口）=====

void HttpServer::Get(const std::string& path, JsonHandler h) {
    m_server->Get(path, [h = std::move(h)](const httplib::Request& req, httplib::Response& res) {
        RequestCtx ctx{req};
        try {
            json body = h(ctx);
            writeJson(res, 200, body);
        } catch (const std::exception& e) {
            writeError(res, 500, "internal_error", e.what());
        }
    });
}

void HttpServer::Post(const std::string& path, JsonHandler h) {
    m_server->Post(path, [h = std::move(h)](const httplib::Request& req, httplib::Response& res) {
        RequestCtx ctx{req};
        try {
            json body = h(ctx);
            // 没显式设过 status 时，POST 默认 200；若 handler 返回 {"_status": 201} 则按其设置
            int status = 200;
            if (body.is_object() && body.contains("_status")) {
                status = body["_status"].get<int>();
                body.erase("_status");
            }
            writeJson(res, status, body);
        } catch (const std::exception& e) {
            writeError(res, 500, "internal_error", e.what());
        }
    });
}

void HttpServer::Put(const std::string& path, JsonHandler h) {
    m_server->Put(path, [h = std::move(h)](const httplib::Request& req, httplib::Response& res) {
        RequestCtx ctx{req};
        try {
            json body = h(ctx);
            int status = 200;
            if (body.is_object() && body.contains("_status")) {
                status = body["_status"].get<int>();
                body.erase("_status");
            }
            writeJson(res, status, body);
        } catch (const std::exception& e) {
            writeError(res, 500, "internal_error", e.what());
        }
    });
}

void HttpServer::Delete(const std::string& path, JsonHandler h) {
    m_server->Delete(path, [h = std::move(h)](const httplib::Request& req, httplib::Response& res) {
        RequestCtx ctx{req};
        try {
            json body = h(ctx);
            writeJson(res, 200, body);
        } catch (const std::exception& e) {
            writeError(res, 500, "internal_error", e.what());
        }
    });
}

// ===== 路由注册（原始接口）=====

void HttpServer::Get(const std::string& path, RawHandler h) {
    m_server->Get(path, std::move(h));
}

void HttpServer::Post(const std::string& path, RawHandler h) {
    m_server->Post(path, std::move(h));
}

void HttpServer::Put(const std::string& path, RawHandler h) {
    m_server->Put(path, std::move(h));
}

void HttpServer::Delete(const std::string& path, RawHandler h) {
    m_server->Delete(path, std::move(h));
}

// ===== 中间件 =====

void HttpServer::setPreRoutingHook(PreHook hook) {
    m_server->set_pre_routing_handler(std::move(hook));
}

void HttpServer::setPostRoutingHook(PostHook hook) {
    m_server->set_post_routing_handler(std::move(hook));
}

void HttpServer::setExceptionHook(ExceptionHook hook) {
    m_server->set_exception_handler(std::move(hook));
}

// ===== 启停 =====

bool HttpServer::listen() {
    {
        std::lock_guard<std::mutex> lk(m_state_mutex);
        m_running = true;
    }
    std::cerr << "[HttpServer] Listening on " << m_host << ":" << m_port << "\n";
    bool ok = m_server->listen(m_host.c_str(), m_port);
    {
        std::lock_guard<std::mutex> lk(m_state_mutex);
        m_running = false;
    }
    return ok;
}

void HttpServer::stop() {
    if (m_server) m_server->stop();
    std::lock_guard<std::mutex> lk(m_state_mutex);
    m_running = false;
}

bool HttpServer::isRunning() const {
    std::lock_guard<std::mutex> lk(m_state_mutex);
    return m_running;
}

// ===== 辅助 =====

void HttpServer::setSSEHeaders(httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");  // 防 nginx 缓冲
}

std::string HttpServer::toJsonString(const json& j) {
    return j.dump();
}

void HttpServer::writeJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void HttpServer::writeError(httplib::Response& res, int status,
                            const std::string& code, const std::string& message) {
    json body = {
        {"error", {
            {"code", code},
            {"message", message},
            {"http_status", status}
        }}
    };
    writeJson(res, status, body);
}

} // namespace kindyun