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
 * @file ExternalService.cpp
 * @brief ExternalServiceRegistry 实现
 */

#include "kindyun/ExternalService.hpp"

#include <algorithm>

namespace kindyun {

ExternalServiceRegistry& ExternalServiceRegistry::instance() {
    static ExternalServiceRegistry inst;
    return inst;
}

void ExternalServiceRegistry::registerService(std::shared_ptr<IExternalService> svc) {
    if (!svc) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_services[svc->name()] = std::move(svc);
}

std::shared_ptr<IExternalService> ExternalServiceRegistry::get(const std::string& name) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_services.find(name);
    if (it == m_services.end()) return nullptr;
    return it->second;
}

std::vector<std::string> ExternalServiceRegistry::listServices() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<std::string> out;
    out.reserve(m_services.size());
    for (auto& kv : m_services) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void ExternalServiceRegistry::clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_services.clear();
}

bool ExternalServiceRegistry::isAllowed(const std::string& name,
                                        const std::vector<std::string>& allowlist,
                                        const std::vector<std::string>& denylist) const {
    // 黑名单优先
    if (std::find(denylist.begin(), denylist.end(), name) != denylist.end()) {
        return false;
    }
    // 白名单为空 = 不限
    if (allowlist.empty()) return true;
    return std::find(allowlist.begin(), allowlist.end(), name) != allowlist.end();
}

} // namespace kindyun