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
 * @file HttpForwardService.cpp
 */

#include "HttpForwardService.hpp"
#include "ServiceBase.hpp"

#include <algorithm>
#include <cctype>

namespace kindyun {

HttpForwardService::HttpForwardService(const json& config) {
    if (config.is_object()) {
        if (config.contains("allowed_hosts") && config["allowed_hosts"].is_array()) {
            for (auto& h : config["allowed_hosts"]) {
                if (h.is_string()) m_allowed_hosts.push_back(h.get<std::string>());
            }
        }
        if (config.contains("denied_hosts") && config["denied_hosts"].is_array()) {
            for (auto& h : config["denied_hosts"]) {
                if (h.is_string()) m_denied_hosts.push_back(h.get<std::string>());
            }
        }
        if (config.contains("timeout_seconds")) {
            m_timeout_seconds = config["timeout_seconds"].get<long>();
            if (m_timeout_seconds <= 0) m_timeout_seconds = 30;
        }
    }
}

bool HttpForwardService::wildcardMatch(const std::string& pattern, const std::string& host) {
    // 支持 *.example.com 形式
    if (pattern == host) return true;
    if (pattern.size() < 3 || pattern[0] != '*' || pattern[1] != '.') return false;

    std::string suffix = pattern.substr(1);  // ".example.com"
    if (host.size() < suffix.size()) return false;
    // host 必须以 suffix 结尾
    if (host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    // suffix 前不能有点（防止 foo.bar.example.com 匹配 *.example.com）
    if (host.size() == suffix.size()) return false;
    if (host[host.size() - suffix.size() - 1] == '.') return true;
    return false;
}

bool HttpForwardService::isHostAllowed(const std::string& url, std::string* reason) const {
    // 解析 URL：取 host 部分
    // 简化处理：找 "://" 之后的第一个 "/"，再之前的部分作为 host
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        if (reason) *reason = "URL missing scheme";
        return false;
    }
    std::string host_port = url.substr(scheme_end + 3);
    auto path_pos = host_port.find('/');
    if (path_pos != std::string::npos) host_port = host_port.substr(0, path_pos);
    auto query_pos = host_port.find('?');
    if (query_pos != std::string::npos) host_port = host_port.substr(0, query_pos);
    auto frag_pos = host_port.find('#');
    if (frag_pos != std::string::npos) host_port = host_port.substr(0, frag_pos);

    // 拆 host:port
    auto colon_pos = host_port.rfind(':');
    std::string host = (colon_pos == std::string::npos) ? host_port : host_port.substr(0, colon_pos);

    // 小写
    std::string host_lc = host;
    std::transform(host_lc.begin(), host_lc.end(), host_lc.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    // 黑名单
    for (auto& d : m_denied_hosts) {
        std::string d_lc = d;
        std::transform(d_lc.begin(), d_lc.end(), d_lc.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (host_lc == d_lc) {
            if (reason) *reason = "host in denied list: " + host;
            return false;
        }
    }

    // 白名单（空 = 不限）
    if (!m_allowed_hosts.empty()) {
        bool ok = false;
        for (auto& a : m_allowed_hosts) {
            std::string a_lc = a;
            std::transform(a_lc.begin(), a_lc.end(), a_lc.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (wildcardMatch(a_lc, host_lc) || host_lc == a_lc) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            if (reason) *reason = "host not in allowed list: " + host;
            return false;
        }
    }

    return true;
}

ExternalResponse HttpForwardService::call(const std::string& action,
                                          const json& params,
                                          const std::string& auth_override) {
    ExternalResponse out;

    // 参数校验
    if (!params.contains("url") || !params["url"].is_string()) {
        out.transport_error = true;
        out.error_message = "params.url (string) is required";
        out.body = {{"error", out.error_message}};
        return out;
    }

    std::string url = params["url"].get<std::string>();

    // 主机白名单/黑名单校验
    std::string reason;
    if (!isHostAllowed(url, &reason)) {
        out.transport_error = true;
        out.http_status = 403;
        out.error_message = "Forbidden: " + reason;
        out.body = {{"error", out.error_message}, {"url", url}};
        return out;
    }

    // method
    std::string method = action;
    std::transform(method.begin(), method.end(), method.begin(),
                   [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE") {
        method = "GET";
    }

    // body / headers
    std::string body;
    std::map<std::string, std::string> headers;
    if (params.contains("body")) {
        if (params["body"].is_string()) {
            body = params["body"].get<std::string>();
        } else {
            body = params["body"].dump();
            headers["Content-Type"] = "application/json";
        }
    }
    if (params.contains("headers") && params["headers"].is_object()) {
        for (auto it = params["headers"].begin(); it != params["headers"].end(); ++it) {
            if (it.value().is_string()) {
                headers[it.key()] = it.value().get<std::string>();
            }
        }
    }
    if (!auth_override.empty()) {
        headers["Authorization"] = auth_override;
    }

    // 请求
    out = httpRequest(method, url, body, headers, m_timeout_seconds);

    if (out.transport_error) {
        out.body = {{"error", out.error_message}, {"url", url}};
    }
    return out;
}

} // namespace kindyun