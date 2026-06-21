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
 * @file HttpForwardService.hpp
 * @brief 通用 HTTP 转发服务（libcurl 出站）
 *
 * 让 KindyunAI 通过受控白名单向任意 HTTPS/HTTP 端点发起请求。
 *
 * 安全：
 *   - 主机名必须在 allowed_hosts 白名单内（支持通配符 *.example.com）
 *   - 主机名不得在 denied_hosts 黑名单内（SSRF 防护：拦截内网/元数据）
 *   - 超时由 timeout_seconds 强制限制
 *
 * 用法：
 *   POST /api/v1/external/http
 *   {"action":"get","params":{"url":"https://api.github.com/zen"}}
 */

#pragma once
#include <string>
#include <vector>
#include <mutex>

#include "kindyun/ExternalService.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace kindyun {

class HttpForwardService : public IExternalService {
public:
    /// 构造时传入 web.external_services.http_forward 配置
    explicit HttpForwardService(const json& config);

    std::string name() const override { return "http"; }
    std::string baseUrl() const override { return "(forwarder)"; }
    std::string description() const override {
        return "Generic HTTP forwarder (libcurl). Subject to host allowlist/denylist.";
    }
    std::vector<std::string> supportedActions() const override {
        return {"get", "post", "put", "delete"};
    }
    ExternalResponse call(
        const std::string& action,
        const json& params,
        const std::string& auth_override = ""
    ) override;

private:
    /// 检查 URL 是否被允许（解析 host 后比对白名单/黑名单）
    bool isHostAllowed(const std::string& url, std::string* reason) const;

    /// 通配符匹配（*.example.com 匹配 a.example.com）
    static bool wildcardMatch(const std::string& pattern, const std::string& host);

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_allowed_hosts;
    std::vector<std::string> m_denied_hosts;
    long m_timeout_seconds = 30;
};

} // namespace kindyun