// ============================================================================
// KindyunAI Agent v1.0.0 - Entry Point
// ============================================================================
//
// Copyright (c) 2026 Kindyun.com. All rights reserved.
// Website  : https://Kindyun.com
// Author   : jayition
// Email    : jayition@qq.com
//
// See VERSION.md for full copyright, license, and version info.
//
// 程序入口：单进程集成 CLI REPL + Web 服务 + 外部接口。
//
// 架构：
//   - 主进程 KindyunAI.exe
//   - 主线程：CLI REPL（runREPL 阻塞）
//   - 后台 std::thread：HttpServer（监听 127.0.0.1:8765）
//   - 共享：SessionManager（一进程一套，CLI 与 Web 会话并存）
//
// 命令行用法：
//   ./KindyunAI [url] [model] [--no-web]
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <curl/curl.h>

#include "kindyun/KindyunGlobal.hpp"
#include "kindyun/Config.hpp"
#include "kindyun/ToolBase.hpp"
#include "kindyun/ToolDispatcher.hpp"
#include "kindyun/ConversationLoop.hpp"
#include "kindyun/LLMClient.hpp"
#include "kindyun/ApprovalManager.hpp"
#include "kindyun/MemoryDB.hpp"
#include "kindyun/KindyunPlugin.hpp"

#include "kindyun/HttpServer.hpp"
#include "kindyun/SessionManager.hpp"
#include "kindyun/ExternalService.hpp"
#include "services/EchoService.hpp"
#include "services/WeatherService.hpp"
#include "services/HttpForwardService.hpp"
#include "ApiGateway.hpp"
#include "ApiHandlers.hpp"

namespace {
    kindyun::HttpServer* g_http_server = nullptr;
    std::atomic<bool> g_shutting_down{false};

    void onSignal(int sig) {
        std::cerr << "\n[KindyunAI] Caught signal " << sig << ", shutting down...\n";
        g_shutting_down = true;
        if (g_http_server) g_http_server->stop();
    }
}



/**
 * printBanner —— 打印程序启动横幅。
 *
 * 在程序启动时显示，包含版本号、功能概述和可用命令摘要。
 */
static void printBanner(const Config& cfg) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "     Kindyun Agent v1.0.0\n";
    std::cout << "     Local AI Assistant (C++17)\n";
    std::cout << "========================================\n";
    std::cout << "  (c) 2026 Kindyun.com — All rights reserved\n";
    std::cout << "  Website: http://www.Kindyun.com\n";
    std::cout << "  Author : jayition <jayition@qq.com>\n";
    std::cout << "----------------------------------------\n";
    if (cfg.web_enabled()) {
        std::cout << "  Web UI    : http://" << cfg.web_host() << ":" << cfg.web_port() << "/\n";
        std::cout << "  API base  : http://" << cfg.web_host() << ":" << cfg.web_port() << "/api/v1/\n";
        std::cout << "  Auth Token: "
                  << (cfg.web_api_token().empty() ? "(disabled, dev mode)" : cfg.web_api_token()) << "\n";
    } else {
        std::cout << "  Web service: disabled\n";
    }
    std::cout << "  LLM       : " << cfg.llm_url() << "  Model: " << cfg.llm_model() << "\n";
    std::cout << "----------------------------------------\n";
    std::cout << "Commands:\n";
    std::cout << "  /new - Start a new conversation\n";
    std::cout << "  /history   - Show message history\n";
    std::cout << "  /save <f>  - Save session to file\n";
    std::cout << "  /load <f>  - Load session from file\n";
    std::cout << "  /help      - Show this help\n";
    std::cout << "  /exit      - Exit\n";
    std::cout << " exit - Exit (alias)\n";
    std::cout << "========================================\n\n";
}

/**
 * printHelp —— 打印详细的命令帮助信息。
 *
 * 在用户输入 /help 时调用，展示所有可用命令及其功能说明。
 */
static void printHelp() {
    std::cout << "\n[KindyunAI Commands]\n";
    std::cout << "  /new       Start a new conversation (clears history)\n";
    std::cout << "  /history   Display current message history\n";
    std::cout << "  /save <path>  Save session to a JSON file\n";
    std::cout << "  /load <path>  Load session from a JSON file\n";
    std::cout << "  /help      Show this help\n";
    std::cout << "  /exit      Exit the agent\n";
    std::cout << "\n";
}

/**
 * runREPL —— 交互式命令行交互循环。
 *
 * 核心交互逻辑：
 *   1. 从 std::cin 读取用户输入（ANSI 编码）
 *   2. 转换为 UTF-8（convert_to_utf8）
 *   3. 解析内置命令：/exit, /new, /help, /save, /load, /history
 *   4. 非命令输入作为对话请求，转发给 ConversationLoop
 *   5. LLM 返回的 UTF-8 响应转换为 GBK（convert_to_gbk）后输出
 *
 * 命令处理优先级：
 *   - /exit：退出程序
 *   - /new：清空对话历史，开始新对话
 *   - /help：显示帮助信息
 *   - /save <path>：保存会话到 JSON 文件
 *   - /load <path>：从 JSON 文件加载会话
 *   - /history：显示对话历史摘要
 *   - 其他：作为对话输入发送给 ConversationLoop
 */
static void runREPL(ConversationLoop& loop) {
    while (true) {
        std::cout << "\n> ";
        std::string raw_input;
        if (!std::getline(std::cin, raw_input)) break;
        if (raw_input.empty()) continue;

        std::string input = convert_to_utf8(raw_input);

        // 命令处理
        if (input == "exit" || input == "/exit") {
            std::cout << "Goodbye!\n";
            break;
        }
        if (input == "/new") {
            loop.clearHistory();
            std::cout << "[New conversation started]\n";
            continue;
        }
        if (input == "/help") {
            printHelp();
            continue;
        }
        if (input.rfind("/save ",0) == 0) {
            std::string path = input.substr(6);
            if (path.empty()) {
                std::cout << "Usage: /save <filepath>\n";
            } else {
                loop.saveSession(path);
                std::cout << "[Session saved to " << path << "]\n";
            }
            continue;
        }
        if (input.rfind("/load ", 0) == 0) {
            std::string path = input.substr(6);
            if (path.empty()) {
                std::cout << "Usage: /load <filepath>\n";
            } else {
                loop.loadSession(path);
                std::cout << "[Session loaded from " << path << "]\n";
            }
            continue;
        }
        if (input == "/history") {
            const auto& hist = loop.history();
            std::cout << "\n[History: " << hist.size() << " messages]\n";
            for (size_t i = 0; i < hist.size(); i++) {
                const auto& m = hist[i];
                std::cout << "  [" << i << "] " << m.role << ": ";
                if (m.content.size() > 100) {
                    std::cout << m.content.substr(0, 100) << "...\n";
                } else {
                    std::cout << m.content << "\n";
                }
            }
            continue;
        }

        //正常对话
        std::cout << "[Thinking...]\n";
        std::string response = loop.run(input);
        std::cout << "\nAI: " << convert_to_gbk(response) << "\n";
    }
}

/**
 * main —— 程序入口点，初始化所有模块并启动 REPL + 可选 Web 服务。
 *
 * 启动流程：
 *   1. curl_global_init + 信号处理
 *   2. Config::instance().load()
 *   3. 注册工具 + 加载插件
 *   4. 初始化 ToolDispatcher
 *   5. 初始化 MemoryDB
 *   6. SessionManager 初始化（共享 dispatcher / config / memory）
 *   7. 创建 CLI 默认会话（interactive 模式）
 *   8. 若 web.enabled 且无 --no-web：注册外部服务 + HttpServer
 *      + 后台 std::thread 启动 listen()
 *   9. printBanner + runREPL（主线程）
 *  10. REPL 退出：HttpServer.stop() + thread.join()
 *
 * 命令行：
 *   ./KindyunAI [url] [model] [--no-web]
 */
int main(int argc, char* argv[]) {
    // 1. libcurl + 信号
    curl_global_init(CURL_GLOBAL_ALL);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // 解析命令行参数
    bool cli_disable_web = false;
    bool cli_disable_repl = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-web") cli_disable_web = true;
        else if (a == "--no-cli" || a == "--web-only") cli_disable_repl = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: KindyunAI [url] [model] [--no-web] [--no-cli]\n"
                      << "  --no-web  Disable the Web/HTTP service (CLI only)\n"
                      << "  --no-cli  Disable the CLI REPL (Web only, run as daemon)\n";
            curl_global_cleanup();
            return 0;
        } else {
            positional.push_back(a);
        }
    }

    // 2. 加载配置
    Config::instance().load("config/config.json");
    const Config& cfg = Config::instance();

    // 3. LLM 客户端参数
    std::string url   = (positional.size() > 0) ? positional[0] : cfg.llm_url();
    std::string model = (positional.size() > 1) ? positional[1] : cfg.llm_model();
    std::cout << "Connecting to: " << url << "\n";
    std::cout << "Model: " << model << "\n";

    // 4. 注册内置工具 + 加载插件
    ToolRegistry::instance().registerAllTools();
    PluginLoader::instance().loadPlugins(".", "plugin", true);

    // 5. 工具分发器
    ToolDispatcher dispatcher;

    // 6. 审批列表（每会话独立 ApprovalManager）
    auto approval_required = cfg.getApprovalRequired();

    // 7. SQLite 记忆数据库
    std::string db_path = cli_disable_web ? "kindyun_memory.db" : cfg.web_session_db_path();
    MemoryDB memoryDB(db_path);
    if (memoryDB.isOpen()) {
        ToolRegistry::instance().setMemoryDB(&memoryDB);
        std::cout << "[MemoryDB] Opened: " << db_path << "\n";
    } else {
        std::cerr << "[MemoryDB] Warning: could not open " << db_path << "\n";
    }

    // 8. SessionManager 初始化（共享）
    kindyun::SessionManager& sm = kindyun::SessionManager::instance();
    sm.init(url, model,
            cfg.http_curl_timeout(), cfg.http_stream_timeout(),
            dispatcher, cfg,
            approval_required, &memoryDB,
            cfg.web_session_max_concurrent());

    // 9. CLI 默认会话（interactive 模式）
    std::string cli_sid = sm.createSession(
        "You are a helpful AI assistant with access to a set of tools. "
        "You can read files, write files, execute commands, search content, "
        "manage memory and todo lists. "
        "When you need to use a tool, respond with a tool call. "
        "Think step by step before taking actions.",
        "cli", 0 /* interactive */);
    if (cli_sid.empty()) {
        std::cerr << "[FATAL] Failed to create CLI session\n";
        curl_global_cleanup();
        return 1;
    }
    ConversationLoop* cli_loop = sm.get(cli_sid);
    if (!cli_loop) {
        std::cerr << "[FATAL] Failed to fetch CLI session\n";
        curl_global_cleanup();
        return 1;
    }

    // 10. Web 服务（后台线程，可选）
    std::unique_ptr<kindyun::HttpServer> http_server;
    std::unique_ptr<std::thread> http_thread;
    std::shared_ptr<kindyun::ApiGateway> gateway;  // heap + shared_ptr，让 web_thread 延长生命周期
    bool web_on = cfg.web_enabled() && !cli_disable_web;
    if (web_on) {
        auto& ext_registry = kindyun::ExternalServiceRegistry::instance();
        ext_registry.clear();
        ext_registry.registerService(std::make_shared<kindyun::EchoService>());
        ext_registry.registerService(std::make_shared<kindyun::WeatherService>());
        ext_registry.registerService(
            std::make_shared<kindyun::HttpForwardService>(cfg.web_external_services_config()));

        http_server = std::make_unique<kindyun::HttpServer>(cfg.web_host(), cfg.web_port());

        std::string token_for_gw = cfg.web_api_token();
        // dev 模式自动判断：绑定本地回环地址时跳过鉴权
        bool dev_mode = (cfg.web_host() == "127.0.0.1"
                      || cfg.web_host() == "::1"
                      || cfg.web_host() == "localhost");
        if (dev_mode) {
            std::cerr << "[Auth] dev mode (loopback host) - token check skipped\n";
        } else if (!token_for_gw.empty()) {
            std::cerr << "[Auth] prod mode (host=" << cfg.web_host()
                      << ") - Bearer token required\n";
        } else {
            std::cerr << "[Auth] prod mode but token is empty - "
                         "all /api/* requests will be REJECTED\n";
        }
        gateway = std::make_shared<kindyun::ApiGateway>(token_for_gw,
                                                          cfg.web_cors_allow_origins(),
                                                          cfg.web_rate_limit_rpm(),
                                                          cfg.web_access_log(),
                                                          dev_mode);
        gateway->install(*http_server);

        kindyun::registerApiHandlers(*http_server, sm, ext_registry, cfg);

        g_http_server = http_server.get();

        // 关键：lambda 捕获 shared_ptr<ApiGateway> 让 gateway 不会被析构
        http_thread = std::make_unique<std::thread>([gateway, &http_server]() {
            http_server->listen();
        });
        std::cerr << "[Web] Server listening on http://"
                  << cfg.web_host() << ":" << cfg.web_port() << "/\n";
    } else {
        std::cerr << "[Web] Service disabled\n";
    }

    // 11. 启动 REPL（主线程）—— 可通过 --no-cli 跳过
    if (!cli_disable_repl) {
        printBanner(cfg);
        runREPL(*cli_loop);
    } else {
        printBanner(cfg);
        std::cerr << "[CLI] REPL disabled (--no-cli). Press Ctrl+C to stop.\n";
        // 主线程阻塞，等信号触发 stop
        while (!g_shutting_down) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    // 12. 收尾
    std::cerr << "\n[KindyunAI] Cleaning up...\n";
    if (http_server) http_server->stop();
    if (http_thread && http_thread->joinable()) http_thread->join();

    g_http_server = nullptr;
    curl_global_cleanup();
    std::cout << "[KindyunAI] Bye.\n";
    return 0;
}
