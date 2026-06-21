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
 * @file ExternalService.hpp
 * @brief 外部服务对接 —— KindyunAI 与第三方服务的桥梁
 *
 * 这是设计文档 §4.3 中定义的"对外服务对接"核心能力。
 *
 * 模型：
 *   - 每个外部服务实现 IExternalService
 *   - 通过 ExternalServiceRegistry 注册
 *   - HTTP 路由 /api/v1/external/{service} 统一转发
 *
 * 安全：
 *   - 服务名受白名单/黑名单约束（来自 config web.external_services.allowlist/denylist）
 *   - HttpForwardService 受 host 白名单约束，禁止内网/元数据地址（SSRF 防护）
 *
 * 用例：
 *   - weather : 天气查询（演示）
 *   - echo    : 回显请求（调试）
 *   - http    : 通用 HTTP 转发（libcurl 出站）
 */

#pragma once
#ifndef KINDYUN_EXTERNAL_SERVICE_HPP
#define KINDYUN_EXTERNAL_SERVICE_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <mutex>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace kindyun {

/**
 * @brief 外部服务响应
 */
struct ExternalResponse {
    int http_status = 200;
    json body;
    std::string raw_body;
    std::map<std::string, std::string> headers;
    long elapsed_ms = 0;
    bool transport_error = false;
    std::string error_message;
};

/**
 * @brief 外部服务接口
 */
class IExternalService {
public:
    virtual ~IExternalService() = default;

    /// 服务名（URL 路径段，如 "weather"）
    virtual std::string name() const = 0;

    /// 基础 URL（展示用，HttpForwardService 返回空）
    virtual std::string baseUrl() const = 0;

    /// 支持的动作列表（展示给客户端）
    virtual std::vector<std::string> supportedActions() const = 0;

    /// 服务的元描述（JSON Schema / 文本说明，可选）
    virtual std::string description() const { return ""; }

    /**
     * @brief 调用服务
     * @param action       动作名
     * @param params       业务参数
     * @param auth_override 鉴权覆盖（空 = 用服务默认）
     */
    virtual ExternalResponse call(
        const std::string& action,
        const json& params,
        const std::string& auth_override = ""
    ) = 0;
};

/**
 * @class ExternalServiceRegistry
 * @brief 外部服务注册中心（单例）
 */
class ExternalServiceRegistry {
public:
    static ExternalServiceRegistry& instance();

    /// 注册一个服务
    void registerService(std::shared_ptr<IExternalService> svc);

    /// 按名称获取
    std::shared_ptr<IExternalService> get(const std::string& name) const;

    /// 列出全部服务名
    std::vector<std::string> listServices() const;

    /// 清空（用于重新加载）
    void clear();

    /// 检查服务是否允许（allowlist/denylist 校验）
    bool isAllowed(const std::string& name,
                   const std::vector<std::string>& allowlist,
                   const std::vector<std::string>& denylist) const;

private:
    ExternalServiceRegistry() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<IExternalService>> m_services;
};

} // namespace kindyun

#endif // KINDYUN_EXTERNAL_SERVICE_HPP