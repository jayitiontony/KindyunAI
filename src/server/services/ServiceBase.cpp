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
 * @file ServiceBase.cpp
 * @brief libcurl 出站 helper 实现
 */

#include "ServiceBase.hpp"

#include <curl/curl.h>

#include <chrono>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace kindyun {

namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t headerCb(char* ptr, size_t size, size_t nitems, void* userdata) {
    auto* hdrs = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t total = size * nitems;
    std::string line(ptr, total);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        // trim
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
            v.pop_back();
        size_t s = 0;
        while (s < v.size() && v[s] == ' ') ++s;
        v = v.substr(s);
        // key 小写
        for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        (*hdrs)[k] = v;
    }
    return total;
}

} // namespace

ExternalResponse httpRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    long timeout_sec
) {
    ExternalResponse out;
    auto t0 = std::chrono::steady_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        out.transport_error = true;
        out.error_message = "curl_easy_init failed";
        return out;
    }

    std::string response_body;
    std::map<std::string, std::string> resp_headers;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "KindyunAIServer/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);

    // SSL: 跳过证书校验（演示用；生产应改为严格校验）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // 自定义 headers
    struct curl_slist* hdrs = nullptr;
    for (auto& kv : headers) {
        std::string line = kv.first + ": " + kv.second;
        hdrs = curl_slist_append(hdrs, line.c_str());
    }
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    out.http_status = static_cast<int>(status);
    out.raw_body = response_body;
    out.headers = resp_headers;
    out.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    if (rc != CURLE_OK) {
        out.transport_error = true;
        out.error_message = curl_easy_strerror(rc);
    }

    // 尝试解析 body 为 JSON（失败则原样塞到 body.raw）
    if (!response_body.empty()) {
        try {
            out.body = json::parse(response_body);
        } catch (...) {
            out.body = json{{"raw", response_body}};
        }
    } else {
        out.body = nullptr;
    }

    return out;
}

std::string urlEncode(const std::string& s) {
    CURL* curl = curl_easy_init();
    if (!curl) return s;
    char* esc = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    std::string out = esc ? esc : "";
    if (esc) curl_free(esc);
    curl_easy_cleanup(curl);
    return out;
}

} // namespace kindyun