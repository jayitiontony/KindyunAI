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
 * @file ServiceBase.hpp
 * @brief 服务公用工具 —— libcurl 出站 HTTP helper
 *
 * 提供：
 *   - libcurl write callback
 *   - 简单 GET / POST helper（返回 ExternalResponse）
 *   - 共享 curl 句柄管理
 *
 * 注意：libcurl 全局已经在 web_main.cpp 入口 curl_global_init，
 *       每个请求用独立 curl_easy_init / curl_easy_cleanup。
 */

#pragma once
#ifndef KINDYUN_SERVICE_BASE_HPP
#define KINDYUN_SERVICE_BASE_HPP

#include <string>
#include <map>

#include "kindyun/ExternalService.hpp"

namespace kindyun {

/**
 * @brief 用 libcurl 发起 HTTP 请求（GET 或 POST）
 *
 * @param method        "GET" | "POST" | "PUT" | "DELETE"
 * @param url           完整 URL
 * @param body          POST body（GET 时传空）
 * @param headers       额外 header（每个 value 一行）
 * @param timeout_sec   超时秒数
 * @return ExternalResponse
 */
ExternalResponse httpRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    long timeout_sec
);

/**
 * @brief URL 编码辅助（只编码必要字符）
 */
std::string urlEncode(const std::string& s);

} // namespace kindyun

#endif // KINDYUN_SERVICE_BASE_HPP