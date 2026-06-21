/**
 * @file ApprovalManager.hpp
 * @brief 危险操作审批管理器
 *
 * 功能：
 *   - 在执行 requires_approval=true 的工具前，等待用户确认
 *   - 支持 /yes /no /y /n风格的交互输入
 *   - 可配置审批模式：always（总是询问）、auto_deny（自动拒绝）、auto_approve（自动放行）
 *
 * 使用场景：
 *   - ConversationLoop::handleToolCall() 在执行危险工具前调用 approve()
 *   - main.cpp 的 REPL 支持审批输入流
 *
 * 配置来源：config.json 中的 tools.approval_required 数组
 */

#pragma once
#include <string>
#include <vector>
#include <functional>

class ApprovalManager {
public:
    /// 审批模式
    enum class Mode {
        interactive,  ///< 等待用户在 stdin 输入确认（REPL 模式）
        auto_approve, ///< 无需确认，直接放行（自动化脚本模式）
        auto_deny     ///< 无需确认，直接拒绝（安全优先模式）
    };

    /**
     * @brief 构造审批管理器
     * @param approval_required 需要审批的工具名列表
     */
    explicit ApprovalManager(const std::vector<std::string>& approval_required);

    /**
     * @brief 设置审批模式
     * @param mode 交互模式
     */
    void setMode(Mode mode);

    /**
     * @brief 检查工具是否需要审批
     * @param toolName 工具名
     * @return 是否需要用户确认
     */
    bool needsApproval(const std::string& toolName) const;

    /**
     * @brief 请求审批（检查 + 等待用户输入）
     * @param toolName 工具名
     * @param args 工具参数描述（用于展示给用户）
     * @return true=用户确认执行，false=用户拒绝
     *
     * @note 在 interactive 模式下会阻塞直到用户输入 y/n
     * 在 auto_*模式下立即返回
     */
    bool approve(const std::string& toolName, const std::string& args = "");

    /**
     * @brief 打印待审批工具信息（给 REPL 用）
     */
    void printPendingApproval(const std::string& toolName,
                              const std::string& args) const;

    /**
     * @brief 静态：检查路径是否在保护路径下（审批前预检）
     */
    static bool isProtectedPath(const std::string& path,
                                 const std::vector<std::string>& protectedPaths);

private:
    std::vector<std::string> m_approval_required;
    Mode m_mode = Mode::interactive;
};