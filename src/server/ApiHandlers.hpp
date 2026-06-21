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
 * @file ApiHandlers.hpp
 * @brief 所有 REST 路由 + UI 路由的注册入口
 *
 * 一站式注册：
 *   registerApiHandlers(server, sm, ext_registry, cfg);
 *
 * 路由清单见 docs/WEB_SERVICE_DESIGN.md §4.2 / §5.1
 */

#pragma once
#ifndef KINDYUN_API_HANDLERS_HPP
#define KINDYUN_API_HANDLERS_HPP

#include "kindyun/HttpServer.hpp"
#include "kindyun/SessionManager.hpp"
#include "kindyun/ExternalService.hpp"

namespace kindyun {

/**
 * @brief 注册全部 REST + UI 路由到 HttpServer
 *
 * 注：cfg 是全局 ::Config，不在 kindyun 命名空间内
 */
void registerApiHandlers(HttpServer& server,
                         SessionManager& sm,
                         ExternalServiceRegistry& ext_registry,
                         const ::Config& cfg);

} // namespace kindyun

#endif // KINDYUN_API_HANDLERS_HPP