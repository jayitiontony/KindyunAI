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
 * @file EchoService.hpp
 * @brief 回显服务（调试用）
 *
 * 不发任何外网请求，直接把 params 原样返回。
 * 用途：联调 / 健康检查 / 教学示例。
 */

#pragma once
#include "kindyun/ExternalService.hpp"

namespace kindyun {

class EchoService : public IExternalService {
public:
    std::string name() const override { return "echo"; }
    std::string baseUrl() const override { return "(in-process)"; }
    std::string description() const override {
        return "Echoes the request back without external calls. Useful for debugging.";
    }
    std::vector<std::string> supportedActions() const override {
        return {"echo", "info"};
    }
    ExternalResponse call(
        const std::string& action,
        const json& params,
        const std::string& auth_override = ""
    ) override;
};

} // namespace kindyun