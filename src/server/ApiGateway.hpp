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
 * @file ApiGateway.hpp
 * @brief API 网关 —— 鉴权 + 限流 + CORS + access log
 *
 * 职责（按请求时序）：
 *   pre_routing:
 *     1. CORS preflight (OPTIONS) 短路放行
 *     2. /api/ 前缀但非 /api/v1/health → 鉴权
 *     3. /api/ 前缀 → 限流检查
 *   post_routing:
 *     4. CORS 头补全到响应
 *     5. access log 输出
 *
 * 安装：
 *   ApiGateway gw(token, cors_origins, rate_limit_rpm, access_log_enabled);
 *   gw.install(server);
 */

#pragma once
#ifndef KINDYUN_API_GATEWAY_HPP
#define KINDYUN_API_GATEWAY_HPP

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <chrono>

#include "kindyun/HttpServer.hpp"

namespace kindyun {

class ApiGateway {
public:
    /**
     * @brief 构造
     * @param token            鉴权 Token（空 = 禁用鉴权）
     * @param cors_origins     CORS 允许的来源列表
     * @param rate_limit_rpm   每分钟每 token 最多请求数（<=0 = 不限）
     * @param access_log       是否启用 access log
     * @param dev_mode         dev 模式（本地回环地址）跳过鉴权 —— 默认 true
     *                          浏览器前端不知道 token 时也能跑；
     *                          生产环境部署到 0.0.0.0 时必须设为 false。
     */
    ApiGateway(std::string token,
               std::vector<std::string> cors_origins,
               int rate_limit_rpm,
               bool access_log,
               bool dev_mode = true);

    /// 安装到 HttpServer
    void install(HttpServer& server);

    /// 调试用：dump 当前 token 状态
    void dumpToken(const char* tag) const;

private:
    /// pre-routing 钩子（返回 Handled = 拦截；Unhandled = 放行）
    httplib::Server::HandlerResponse preRouting(const httplib::Request& req, httplib::Response& res);

    /// post-routing 钩子（CORS 头补全 + access log）
    void postRouting(const httplib::Request& req, httplib::Response& res);

    /// 检查并扣减限流配额（按 token 分桶）
    bool checkRateLimit(const std::string& token);

    /// 是否需要鉴权（路径判断）
    static bool requiresAuth(const std::string& path);

    /// 取 Bearer token（无则返回空）
    static std::string extractBearer(const httplib::Request& req);

    /// 输出 access log
    void logAccess(const httplib::Request& req, const httplib::Response& res);

private:
    std::string m_token;
    std::vector<std::string> m_cors_origins;
    int m_rate_limit_rpm;
    bool m_access_log;
    bool m_dev_mode = true;

    // 限流桶：token -> deque<时间戳>
    std::mutex m_rate_mutex;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> m_buckets;
};

} // namespace kindyun

#endif // KINDYUN_API_GATEWAY_HPP