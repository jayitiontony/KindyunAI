KindyunAI 发布说明 (Release Notes) — Version 1.0.0
   *发布日期:* 2026-06-21

   *概述:* 欢迎使用 KindyunAI v1.0.0！这是 KindyunAI 本地 AI 助手的首个稳定版本。本次发布将命令行 REPL、Web 服务与外部 API 集成于单一进程中，提供了完整的 RESTful API、流式响应支持、内置安全机制以及开箱即用的 Web 前端界面。你只需要在本地搭一个本地AI建议使用llama.cpp搭建，非常简单。

   *🌟 新增功能 (New Features)*
   - **单进程一体化架构**：`KindyunAI.exe` 同时运行 CLI REPL、Web 服务与外部 API，简化部署。
   - **Web 管理界面**：内置深色主题单页应用（Vanilla JS），提供直观的浏览器前端。
   - **完整的 REST API**：支持会话管理、同步/流式聊天、工具列表与调用、外部服务适配等。
   - **流式响应 (SSE) 增强**：支持推理模型 (`reasoning_content`)、工具调用流式累积 (`delta.tool_calls`)，兼容完整的事件流 (`meta`, `delta`, `tool`, `tool_result`, `done`, `error`)。
   - **内置外部服务**：提供 `echo`（调试）、`weather`（wttr.in 天气查询）、`http`（通用 HTTP 转发，带主机白名单与 SSRF 防护）。
   - **安全机制**：Bearer Token 认证（支持回环地址自动放行）、CORS、令牌级速率限制（默认 120 次/分钟）、路径黑名单（如 `/etc`, `C:\Windows`）。
   - **可视化配置**：支持通过 `config/config.json` 的 `web` 段进行灵活配置。

   *🐛 问题修复 (Bug Fixes)*
   - 修复了 CLI REPL 在 `Ctrl+C` 退出后的悬空 `this` 指针问题（改用 `shared_ptr`）。
   - 修正 `cpp-httplib` 在 Windows 下的版本兼容性问题（配置 `_WIN32_WINNT=0x0A00`）。
   - 修复静态资源路由匹配问题（优化 `/static/*` 为正则路由）。
   - 完善推理模型 SSE 支持，正确读取 `delta.reasoning_content`。
   - 优化流式工具调用解析，按 `index` 正确累积 `delta.tool_calls`。
   - 修复循环地址（loopback）的 Token 认证逻辑，开发模式下自动跳过验证。

   *📦 运行环境与依赖 (Runtime & Dependencies)*
   - **编译平台**：Windows x86_64 (MinGW-w64 GCC 14.2)
   - **运行依赖**：`libcurl-x64`, `libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`
   - **第三方组件**：cpp-httplib v0.28, nlohmann/json, libcurl, SQLite

   *📖 核心接口速览 (API Highlights)*
   (List key endpoints briefly)
   - `GET /api/v1/health` — 服务健康检查
   - `POST/GET/DELETE /api/v1/sessions` — 会话管理
   - `POST /api/v1/chat` & `/api/v1/chat/stream` — 同步/流式对话
   - `GET /api/v1/tools` & `POST /api/v1/tools/{name}/invoke` — 工具管理与调用
   - `POST /api/v1/external/{service}` — 外部服务适配器

   *📜 许可与版权 (License & Copyright)*
   - 版权：© 2026 Kindyun.com. All rights reserved.
   - 许可协议：专有软件 (Proprietary)，允许项目内使用、学习与修改。禁止未经授权的对外分发、逆向工程及删除版权标识。
   - 联系作者：jayition <jayition@qq.com>
   - 官方网站：http://Kindyun.com

   *🙏 致谢 (Acknowledgments)*
   感谢 cpp-httplib, nlohmann/json, libcurl, SQLite 等开源项目为本次发布提供的支持。

   *注：本文档最后更新于 2026-06-21。*
