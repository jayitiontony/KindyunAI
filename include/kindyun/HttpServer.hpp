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
 * @file HttpServer.hpp
 * @brief HTTP 服务器封装 —— 基于 cpp-httplib
 *
 * KindyunAIServer.exe 的 HTTP 入口。
 * 封装 cpp-httplib 的 Server，提供：
 *   - 路由注册便捷方法（Get/Post/Delete/Put）
 *   - JSON 请求/响应辅助
 *   - 全局中间件钩子（pre-routing / post-routing / exception）
 *   - 优雅启停（listen + stop）
 *
 * 设计说明：
 *   - 入站 HTTP 由 cpp-httplib 负责
 *   - 出站 HTTP 仍由 libcurl 负责（LLM 通信 + 外部服务适配器）
 *   - 两者职责互补，本类只关心入站
 *
 * @see cpp-httplib 单 header 文件位于 ../cpp-httplib/httplib.h
 */

#pragma once
#ifndef KINDYUN_HTTP_SERVER_HPP
#define KINDYUN_HTTP_SERVER_HPP

#include <string>
#include <functional>
#include <memory>
#include <mutex>

// httplib 是 header-only 库，include 路径由构建系统指定
#include <httplib.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace kindyun {

/**
 * @brief HTTP 请求处理器类型（原始）
 *
 * 与 cpp-httplib 原生签名一致，便于直接使用其全部能力。
 */
using RawHandler = std::function<void(const httplib::Request&, httplib::Response&)>;

/**
 * @brief 简化请求上下文
 *
 * 把 cpp-lib 原生 Request 包装一层，提供：
 *   - path / method / query 便捷访问
 *   - body 解析为 nlohmann::json（解析失败抛异常）
 *   - header / param 访问
 */
struct RequestCtx {
    const httplib::Request& req;

    std::string method() const { return req.method; }
    std::string path()   const { return req.path; }
    std::string body()   const { return req.body; }

    /// 解析 body 为 JSON，失败返回空对象（不抛异常）
    json jsonBody() const {
        if (req.body.empty()) return json::object();
        try { return json::parse(req.body); }
        catch (...) { return json::object(); }
    }

    /// 路径参数（如 /sessions/{id} 中的 id）
    std::string pathParam(const std::string& name) const {
        auto it = req.path_params.find(name);
        if (it == req.path_params.end()) return "";
        return it->second;
    }

    /// Header 取值（大小写不敏感）
    std::string header(const std::string& name) const {
        return req.get_header_value(name);
    }

    /// Query 参数
    std::string query(const std::string& name) const {
        auto it = req.params.find(name);
        if (it == req.params.end()) return "";
        return it->second;
    }
};

/**
 * @brief JSON 处理器类型
 *
 * 直接吐 JSON 响应，写起来最简洁。
 */
using JsonHandler = std::function<json(const RequestCtx&)>;

/**
 * @class HttpServer
 * @brief HTTP 服务器封装
 *
 * 用法：
 *   HttpServer srv("127.0.0.1", 8765);
 *   srv.Get("/hello", [](const RequestCtx&) {
 *       return json{{"msg", "hi"}};
 *   });
 *   srv.listen();  // 阻塞，直到 stop() 被调用
 */
class HttpServer {
public:
    /**
     * @brief 构造 HTTP 服务器
     * @param host 监听地址（如 "127.0.0.1"、"0.0.0.0"）
     * @param port 监听端口
     */
    HttpServer(const std::string& host, int port);

    ~HttpServer();

    // ===== 路由注册（JSON 便捷接口）=====
    /** @brief 注册 GET 路由 */
    void Get(const std::string& path, JsonHandler h);
    /** @brief 注册 POST 路由 */
    void Post(const std::string& path, JsonHandler h);
    /** @brief 注册 PUT 路由 */
    void Put(const std::string& path, JsonHandler h);
    /** @brief 注册 DELETE 路由 */
    void Delete(const std::string& path, JsonHandler h);

    // ===== 路由注册（原始接口，给需要流式/特殊响应的 handler）=====
    void Get(const std::string& path, RawHandler h);
    void Post(const std::string& path, RawHandler h);
    void Put(const std::string& path, RawHandler h);
    void Delete(const std::string& path, RawHandler h);

    // ===== 全局中间件钩子 =====
    /**
     * @brief 注册 pre-routing 钩子（鉴权/限流/CORS preflight）
     * 返回 Handled 表示已被拦截（hook 已写响应，路由不再处理）
     * 返回 Unhandled 表示继续走路由
     */
    using PreHook = std::function<httplib::Server::HandlerResponse(const httplib::Request&, httplib::Response&)>;
    void setPreRoutingHook(PreHook hook);

    /**
     * @brief 注册 post-routing 钩子（access log）
     * 在每个响应写出后调用（注：此时 Response 仍可修改，CORS 头等可在此补全）
     */
    using PostHook = std::function<void(const httplib::Request&, httplib::Response&)>;
    void setPostRoutingHook(PostHook hook);

    /**
     * @brief 注册全局异常处理器
     * 当任何 handler 抛出异常时被调用
     */
    using ExceptionHook = std::function<void(const httplib::Request&, httplib::Response&, std::exception_ptr)>;
    void setExceptionHook(ExceptionHook hook);

    // ===== 启停 =====
    /** @brief 启动并阻塞直到 stop() 被调用 */
    bool listen();
    /** @brief 异步停止服务器 */
    void stop();
    /** @brief 是否正在监听 */
    bool isRunning() const;

    /**
     * @brief 设置 SSE（Server-Sent Events）响应头
     *
     * @param res httplib::Response
     */
    static void setSSEHeaders(httplib::Response& res);

    /**
     * @brief 把 nlohmann::json 转为 JSON 字符串（UTF-8）
     */
    static std::string toJsonString(const json& j);

    /**
     * @brief 写入 JSON 响应（自动设 Content-Type）
     */
    static void writeJson(httplib::Response& res, int status, const json& body);

    /**
     * @brief 写入错误响应（统一错误格式）
     */
    static void writeError(httplib::Response& res, int status,
                           const std::string& code, const std::string& message);

private:
    std::string m_host;
    int m_port;
    std::unique_ptr<httplib::Server> m_server;
    mutable std::mutex m_state_mutex;
    bool m_running = false;
};

} // namespace kindyun

#endif // KINDYUN_HTTP_SERVER_HPP