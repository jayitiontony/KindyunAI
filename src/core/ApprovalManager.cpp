/**
 * @file ApprovalManager.cpp
 * @brief 审批管理器实现
 */

#include "kindyun/ApprovalManager.hpp"
#include <iostream>
#include <algorithm>

ApprovalManager::ApprovalManager(const std::vector<std::string>& approval_required)
    : m_approval_required(approval_required) {}

void ApprovalManager::setMode(Mode mode) {
    m_mode = mode;
}

bool ApprovalManager::needsApproval(const std::string& toolName) const {
    return std::find(m_approval_required.begin(),
                     m_approval_required.end(),
                     toolName) != m_approval_required.end();
}

bool ApprovalManager::approve(const std::string& toolName, const std::string& args) {
    if (!needsApproval(toolName)) return true;

    switch (m_mode) {
        case Mode::auto_approve:
            return true;
        case Mode::auto_deny:
            std::cerr << "[ApprovalManager] Auto-denied: " << toolName << std::endl;
            return false;
        case Mode::interactive:
            printPendingApproval(toolName, args);
            // 从 stdin 读取确认
            std::string line;
            if (!std::getline(std::cin, line)) return false;
            char c = line.empty() ? 0 : line[0];
            if (c == 'y' || c == 'Y') return true;
            return false;
    }
    return false;
}

void ApprovalManager::printPendingApproval(
    const std::string& toolName,
    const std::string& args
) const {
    std::cout << "\n[Approval Required] ";
    std::cout << "Confirm execution of: " << toolName;
    if (!args.empty()) {
        std::cout << "\n  Args: " << args;
    }
    std::cout << "\n  (y/n): ";
    std::cout.flush();
}

bool ApprovalManager::isProtectedPath(
    const std::string& path,
    const std::vector<std::string>& protectedPaths
) {
    for (const auto& p : protectedPaths) {
        if (path.find(p) == 0) return true;
    }
    return false;
}