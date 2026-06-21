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
 * @file EchoService.cpp
 */

#include "EchoService.hpp"

#include <chrono>

namespace kindyun {

ExternalResponse EchoService::call(const std::string& action,
                                   const json& params,
                                   const std::string& /*auth_override*/) {
    ExternalResponse out;
    out.http_status = 200;
    out.body = {
        {"service", "echo"},
        {"action", action},
        {"params", params},
        {"ts", std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    out.raw_body = out.body.dump();
    out.elapsed_ms = 0;
    return out;
}

} // namespace kindyun