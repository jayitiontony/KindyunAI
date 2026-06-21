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
 * @file ApiGateway.cpp
 */

#include "ApiGateway.hpp"

#include <cstdio>
#include <iostream>
#include <algorithm>

namespace kindyun {

ApiGateway::ApiGateway(std::string token,
                       std::vector<std::string> cors_origins,
                       int rate_limit_rpm,
                       bool access_log,
                       bool dev_mode)
    : m_token(std::move(token)),
      m_cors_origins(std::move(cors_origins)),
      m_rate_limit_rpm(rate_limit_rpm),
      m_access_log(access_log),
      m_dev_mode(dev_mode) {}

void ApiGateway::install(HttpServer& server) {
    server.setPreRoutingHook([this](const httplib::Request& req, httplib::Response& res) {
        return preRouting(req, res);
    });
    server.setPostRoutingHook([this](const httplib::Request& req, httplib::Response& res) {
        postRouting(req, res);
    });
}

void ApiGateway::dumpToken(const char* tag) const {
    (void)tag;  // 调试用，保留接口但 no-op
}

httplib::Server::HandlerResponse ApiGateway::preRouting(const httplib::Request& req, httplib::Response& res) {
    using HandlerResponse = httplib::Server::HandlerResponse;
    const std::string& path = req.path;

    // 1. CORS preflight 短路
    if (req.method == "OPTIONS") {
        for (auto& o : m_cors_origins) res.set_header("Access-Control-Allow-Origin", o);
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "600");
        res.status = 204;
        return HandlerResponse::Handled;  // 拦截
    }

    // 非 /api/* 直接放行
    if (path.rfind("/api/", 0) != 0) return HandlerResponse::Unhandled;

    // 2. 鉴权
    if (requiresAuth(path)) {
        // dev 模式（绑定到本地回环地址）跳过鉴权，方便浏览器前端无 token 跑
        if (m_dev_mode) {
            // 放行
        } else if (m_token.empty()) {
            // token 显式为空 = 鉴权关闭
        } else {
            std::string bearer = extractBearer(req);
            if (bearer != m_token) {
                HttpServer::writeError(res, 401, "unauthorized",
                    "Missing or invalid Authorization Bearer token");
                return HandlerResponse::Handled;
            }
        }
    }

    // 3. 限流
    if (m_rate_limit_rpm > 0) {
        std::string bucket_key = extractBearer(req);
        if (bucket_key.empty()) bucket_key = req.remote_addr;
        if (!checkRateLimit(bucket_key)) {
            HttpServer::writeError(res, 429, "rate_limited",
                "Too many requests. Limit: " + std::to_string(m_rate_limit_rpm) + " req/min");
            return HandlerResponse::Handled;
        }
    }

    return HandlerResponse::Unhandled;
}

void ApiGateway::postRouting(const httplib::Request& req, httplib::Response& res) {
    for (auto& o : m_cors_origins) res.set_header("Access-Control-Allow-Origin", o);
    res.set_header("Vary", "Origin");

    if (m_access_log) logAccess(req, res);
}

bool ApiGateway::requiresAuth(const std::string& path) {
    // /api/v1/health 公开
    if (path == "/api/v1/health") return false;
    if (path == "/api/v1/healthz") return false;
    if (path == "/api/v1/version") return false;
    // 其余 /api/* 都要鉴权
    return path.rfind("/api/", 0) == 0;
}

std::string ApiGateway::extractBearer(const httplib::Request& req) {
    std::string h = req.get_header_value("Authorization");
    if (h.empty()) return "";
    constexpr const char* prefix = "Bearer ";
    constexpr size_t plen = 7;
    if (h.size() > plen && h.compare(0, plen, prefix) == 0) {
        return h.substr(plen);
    }
    return "";
}

bool ApiGateway::checkRateLimit(const std::string& key) {
    std::lock_guard<std::mutex> lk(m_rate_mutex);
    auto& bucket = m_buckets[key];
    auto now = std::chrono::steady_clock::now();

    // 弹出 60 秒之前的记录
    while (!bucket.empty() && now - bucket.front() > std::chrono::seconds(60)) {
        bucket.pop_front();
    }

    if (static_cast<int>(bucket.size()) >= m_rate_limit_rpm) return false;
    bucket.push_back(now);
    return true;
}

void ApiGateway::logAccess(const httplib::Request& req, const httplib::Response& res) {
    // KindyunAI access log (c) 2026 Kindyun.com
    std::cerr << "[KindyunAI/access] "
              << req.remote_addr << " "
              << req.method << " "
              << req.path
              << " -> " << res.status << "\n";
}

} // namespace kindyun