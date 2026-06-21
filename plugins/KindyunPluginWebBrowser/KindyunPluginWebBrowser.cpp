/**
 * @file KindyunPluginWebBrowser.cpp
 * @brief KindyunAI Web Browser 插件实现
 *
 * 这是一个 KindyunAI 插件 DLL，实现网页访问功能。
 * 使用 libcurl 库获取网页内容，并将文字内容反馈给模型。
 *
 * 工作流程：
 *   1. 解析 JSON 参数（url、timeout、max_length 等）
 *   2. 使用 libcurl 发送 HTTP GET 请求
 *   3. 处理重定向（如启用）
 *   4. 提取纯文本内容（去除 HTML 标签）
 *   5. 限制返回内容长度
 *   6. 返回结果字符串
 *
 * 依赖库：libcurl（必须链接 libcurl.dll.a 或 libcurl.lib）
 */

#include "KindyunPluginWebBrowser.hpp"
#include <curl/curl.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdlib>

// ============================================================================
// 静态配置（插件全局配置）
// ============================================================================

static PluginConfig g_config;  ///< 插件全局配置实例
static std::string g_last_result;  ///< 最后一次执行结果（用于 cleanup_tool）

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief libcurl 写入回调函数
 *
 * libcurl 在收到数据时调用此函数，将数据追加到 std::string。
 *
 * @param contents 接收到的数据指针
 * @param size 每个元素的大小
 * @param nmemb 元素数量
 * @param userdata 指向 std::string 的指针（用于存储接收到的数据）
 * @return 实际写入的字节数
 */
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
    size_t total_size = size * nmemb;
    if (out) {
        out->append(static_cast<char*>(contents), total_size);
    }
    return total_size;
}

/**
 * @brief 简单的 HTML 标签移除函数
 *
 * 将 HTML 内容中的标签（如 <p>、<div>、<script> 等）移除，
 * 保留纯文本内容。这是一个简化版本，不处理所有边缘情况。
 *
 * @param html HTML 字符串
 * @return 纯文本字符串
 */
static std::string RemoveHtmlTags(const std::string& html) {
    std::string result;
    bool in_tag = false;

    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];

        if (c == '<') {
            in_tag = true;
            continue;
        }

        if (c == '>') {
            in_tag = false;
            continue;
        }

        if (!in_tag) {
            // 跳过多个连续空格，保留单个空格
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!result.empty() && result.back() != ' ') {
                    result += ' ';
                }
            } else {
                result += c;
            }
        }
    }

    // 去除首尾空白
    if (!result.empty()) {
        size_t start = result.find_first_not_of(" \t\n\r");
        size_t end = result.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            result = result.substr(start, end - start + 1);
        }
    }

    return result;
}

/**
 * @brief 解析 JSON 参数（简化版本）
 *
 * 从 JSON 字符串中提取指定字段的值。
 * 这是一个简化版本，不处理复杂的 JSON 结构。
 *
 * @param json_str JSON 字符串
 * @param key 字段名
 * @return 字段值（如果存在）
 */
static std::string JsonParseString(const std::string& json_str, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json_str.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }

    pos = json_str.find(':', pos + search_key.length());
    if (pos == std::string::npos) {
        return "";
    }

    pos = json_str.find('"', pos + 1);
    if (pos == std::string::npos) {
        return "";
    }

    size_t end = json_str.find('"', pos + 1);
    if (end == std::string::npos) {
        return "";
    }

    return json_str.substr(pos + 1, end - pos - 1);
}

/**
 * @brief 解析 JSON 整数参数
 *
 * @param json_str JSON 字符串
 * @param key 字段名
 * @param default_val 默认值
 * @return 整数值
 */
static int JsonParseInt(const std::string& json_str, const std::string& key, int default_val) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json_str.find(search_key);
    if (pos == std::string::npos) {
        return default_val;
    }

    pos = json_str.find(':', pos + search_key.length());
    if (pos == std::string::npos) {
        return default_val;
    }

    pos = json_str.find_first_of("0123456789", pos + 1);
    if (pos == std::string::npos) {
        return default_val;
    }

    try {
        return std::stoi(json_str.substr(pos), nullptr);
    } catch (...) {
        return default_val;
    }
}

// ============================================================================
// 插件接口实现
// ============================================================================

/**
 * @brief get_plugin_name —— 返回插件名称
 *
 * 插件名称用于标识插件，将在日志中显示。
 *
 * @return 插件名称字符串 "WebBrowser"
 */
WEBBROWSER_EXPORT const char* get_plugin_name() {
    return "WebBrowser";
}

/**
 * @brief get_tool_name —— 返回工具名称
 *
 * 工具名称是全局唯一的，将在 ToolRegistry 中注册时使用。
 *
 * @return 工具名称字符串 "web_browser"
 */
WEBBROWSER_EXPORT const char* get_tool_name() {
    return "web_browser";
}

/**
 * @brief get_tool_description —— 返回工具描述
 *
 * 工具描述发送给 LLM，帮助模型判断是否调用此工具。
 * 描述中应包含工具的用途、输入参数和输出内容。
 *
 * @return 工具功能描述字符串
 */
WEBBROWSER_EXPORT const char* get_tool_description() {
    return "web_browser —— 访问网页并获取文字内容。通过 HTTP GET 请求获取指定 URL 的网页内容，"
           "移除 HTML 标签后返回纯文本。适用于获取网页信息、阅读文章、查询在线数据等场景。"
           "参数：url（必需，网页 URL），timeout（可选，超时秒数），max_length（可选，最大返回长度）。";
}

/**
 * @brief get_tool_parameters —— 返回工具参数定义（JSON Schema 格式）
 *
 * 参数定义描述了工具接受的参数结构，包括类型、必填项和约束。
 * 此函数返回的 JSON 字符串将作为工具调用的参数 Schema。
 *
 * @return JSON 字符串，定义工具的参数结构
 */
WEBBROWSER_EXPORT const char* get_tool_parameters() {
    return "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\",\"description\":\"网页 URL\"},\"timeout\":{\"type\":\"integer\",\"description\":\"超时秒数\",\"default\":15},\"max_length\":{\"type\":\"integer\",\"description\":\"最大返回内容长度\",\"default\":5000},\"user_agent\":{\"type\":\"string\",\"description\":\"User-Agent 字符串\"}},\"required\":[\"url\"]}";
}

/**
 * @brief execute_tool —— 执行网页访问
 *
 * 核心执行逻辑：
 *   1. 解析 JSON 参数（url、timeout、max_length 等）
 *   2. 验证必需参数（url）
 *   3. 创建 libcurl 实例并设置选项
 *   4. 执行 HTTP GET 请求
 *   5. 处理重定向（如启用）
 *   6. 移除 HTML 标签，提取纯文本
 *   7. 限制返回内容长度
 *   8. 返回结果字符串
 *
 * @param arguments JSON 字符串，包含参数：
 *   - "url" (必需): 要访问的网页 URL
 *   - "timeout" (可选): 超时时间（秒）
 *   - "max_length" (可选): 最大返回内容长度
 *   - "user_agent" (可选): User-Agent 字符串
 * @return 网页内容文本（成功）或错误信息（失败）
 */
WEBBROWSER_EXPORT const char* execute_tool(const char* arguments) {
    if (!arguments || std::string(arguments).empty()) {
        g_last_result = "Error: arguments is empty";
        return g_last_result.c_str();
    }

    // 1. 解析参数
    std::string json_str(arguments);
    std::string url = JsonParseString(json_str, "url");
    int timeout = JsonParseInt(json_str, "timeout", g_config.timeout_seconds);
    int max_length = JsonParseInt(json_str, "max_length", g_config.max_content_length);
    std::string user_agent = JsonParseString(json_str, "user_agent");

    if (url.empty()) {
        g_last_result = "Error: 'url' parameter is required";
        return g_last_result.c_str();
    }

    // 设置 User-Agent（如果提供）
    if (user_agent.empty()) {
        user_agent = g_config.user_agent;
    }

    // 2. 初始化 libcurl
    CURL* curl = curl_easy_init();
    if (!curl) {
        g_last_result = "Error: Failed to initialize curl";
        return g_last_result.c_str();
    }

    // 3. 设置 curl 选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);  // 使用静态变量存储结果
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, g_config.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, g_config.max_redirects);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());

    // 4. 执行请求
    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    CURLcode res = curl_easy_perform(curl);

    // 5. 检查结果
    if (res != CURLE_OK) {
        const char* error_msg = curl_easy_strerror(res);
        g_last_result = "Error: curl request failed (";
        g_last_result += error_msg;
        g_last_result += ")";
        curl_easy_cleanup(curl);
        return g_last_result.c_str();
    }

    // 6. 获取 HTTP 状态码
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200) {
        g_last_result = "Error: HTTP ";
        g_last_result += std::to_string(http_code);
        g_last_result += " response";
        curl_easy_cleanup(curl);
        return g_last_result.c_str();
    }

    // 7. 移除 HTML 标签，提取纯文本
    std::string text_content = RemoveHtmlTags(response_data);

    // 8. 限制返回内容长度
    if ((int)text_content.length() > max_length) {
        text_content = text_content.substr(0, max_length);
        text_content += "\n...(content truncated, max_length: " + std::to_string(max_length) + ")";
    }

    // 9. 构造结果
    g_last_result = "URL: ";
    g_last_result += url;
    g_last_result += "\n\n";
    g_last_result += text_content;

    // 清理
    curl_easy_cleanup(curl);
    return g_last_result.c_str();
}

/**
 * @brief cleanup_tool —— 清理插件资源
 *
 * 在插件卸载时调用，释放所有分配的资源。
 * 当前实现主要清理静态变量。
 */
WEBBROWSER_EXPORT void cleanup_tool() {
    // 清理静态变量
    g_last_result.clear();

    // 清理 libcurl 全局资源
    curl_global_cleanup();
}
