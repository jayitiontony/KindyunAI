/**
 * @file KindyunPluginWebBrowser.hpp
 * @brief KindyunAI Web Browser 插件头文件
 *
 * 这是一个 KindyunAI 插件 DLL，实现网页访问功能。
 * 使用 libcurl 库获取网页内容，并将文字内容反馈给模型。
 *
 * 插件导出函数（遵循 KindyunPlugin 接口规范）：
 *   - get_plugin_name()      : 返回插件名称 "WebBrowser"
 *   - get_tool_name()        : 返回工具名称 "web_browser"
 *   - get_tool_description() : 返回工具描述（用于 LLM 提示）
 *   - get_tool_parameters()  : 返回 JSON Schema 参数定义
 *   - execute_tool()         : 执行网页访问并返回内容
 *   - cleanup_tool()         : 清理插件资源
 */

#ifndef KINDYUN_PLUGIN_WEB_BROWSER_HPP
#define KINDYUN_PLUGIN_WEB_BROWSER_HPP

#include <curl/curl.h>
#include <string>
#include "kindyun/KindyunPlugin.hpp"

// ============================================================================
// 插件导出声明
// ============================================================================

#ifdef KINDYUN_PLUGIN_EXPORTS
    #define WEBBROWSER_EXPORT extern "C" __declspec(dllexport)
#else
    #define WEBBROWSER_EXPORT extern "C" __declspec(dllimport)
#endif

// ============================================================================
// 插件配置
// ============================================================================

/**
 * @brief 插件默认配置
 *
 * 包含网页访问的超时设置、最大内容长度、默认用户代理等参数。
 * 可通过 execute_tool 的参数覆盖默认值。
 */
struct PluginConfig {
    int timeout_seconds = 15;       ///< 请求超时时间（秒）
    int max_content_length = 5000;  ///< 最大返回内容长度（字符数）
    std::string user_agent = "KindyunAI/1.0 (Plugin/WebBrowser)";  ///< User-Agent
    bool follow_redirects = true;   ///< 是否跟随重定向
    int max_redirects = 5;          ///< 最大重定向次数
};

// ============================================================================
// 插件接口实现声明
// ============================================================================

/**
 * @brief 获取插件名称
 * @return 插件名称字符串 "WebBrowser"
 */
WEBBROWSER_EXPORT const char* get_plugin_name();

/**
 * @brief 获取工具名称
 * @return 工具名称字符串 "web_browser"
 */
WEBBROWSER_EXPORT const char* get_tool_name();

/**
 * @brief 获取工具描述
 * @return 工具功能描述（发送给 LLM）
 */
WEBBROWSER_EXPORT const char* get_tool_description();

/**
 * @brief 获取工具参数定义（JSON Schema 格式）
 * @return JSON 字符串，定义工具的参数结构
 */
WEBBROWSER_EXPORT const char* get_tool_parameters();

/**
 * @brief 执行网页访问工具
 *
 * 使用 libcurl 获取指定 URL 的网页内容，并进行以下处理：
 *   1. 发送 HTTP GET 请求
 *   2. 处理重定向（如启用）
 *   3. 提取纯文本内容
 *   4. 限制返回内容长度
 *
 * @param arguments JSON 字符串，包含参数：
 *   - "url" (必需): 要访问的网页 URL
 *   - "timeout" (可选): 超时时间（秒），覆盖默认值
 *   - "max_length" (可选): 最大返回内容长度，覆盖默认值
 *   - "user_agent" (可选): User-Agent 字符串
 * @return 网页内容文本（成功）或错误信息（失败）
 */
WEBBROWSER_EXPORT const char* execute_tool(const char* arguments);

/**
 * @brief 清理插件资源
 */
WEBBROWSER_EXPORT void cleanup_tool();

#endif // KINDYUN_PLUGIN_WEB_BROWSER_HPP
