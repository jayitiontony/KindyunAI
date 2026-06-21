KindyunAI 发布说明 (Release Notes) — Version 1.0.0
   *发布日期:* 2026-06-21
### 📜 项目背景
KindyunAI 诞生于对主流 AI 助手“体积臃肿、依赖复杂、强绑线上”这一痛点的长期观察。作为一名拥有二十余年 C++ 开发经验的工程师，我始终偏爱 C++ 的精炼与高效，渴望打造一款真正“绿色”的 AI 助手——无需安装运行库、无需配置虚拟环境，复制解压即可运行。项目历时一个月从零搭建核心框架，开发过程中深度借助 AI 进行查错、注释与文档生成，验证了“AI 辅助开发”的提效潜力。在本地调试（如使用 35B 模型处理简单语法错误）的过程中，我也深刻体会到：AI 虽能依靠算力逐步逼近答案，但程序员的架构直觉与全局视野仍是不可替代的核心。最终，KindyunAI 以仅约 3MB 的体积交付，实现了“开箱即用、环境无关”的初心。

### 🚀 项目愿景
KindyunAI 旨在构建一个**轻量、本地优先、高度可扩展**的 AI 工具生态。我们摒弃了捆绑数百 MB 组件和固定云端服务的路径，转而追求“小而精、重内核”的设计哲学。项目已实现标准化插件架构：只需遵循约定的文件命名与接口规范，开发者即可编写 DLL 供 AI 自动识别与调用（目前内置 Web 访问等示例插件）。未来，我们期待以 KindyunAI 为起点抛砖引玉，吸引更多开发者参与共建，孵化更多轻量级 AI 辅助工具。最终，让开发者不再受制于臃肿的安装包与弱网环境，拥有一款**资源克制、按需加载、算力本地化**的 AI 助手，将人类的创造思维与 AI 的执行效率无缝融合。

💡 **使用建议**：
- 若用于 `README.md`，可直接将上述内容置于 `## 项目背景` / `## 愿景与目标` 章节下。
- 若用于发布会/博客，可保留首段加粗关键词，增强传播性。
- 如需补充技术栈清单（如：`C++17 / Qt / llama.cpp / Qwen / DLL Plugin System`），可在此基础上按需追加。
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
<img width="1982" height="1481" alt="image" src="https://github.com/user-attachments/assets/fdfa0a28-17b9-4ba5-a250-5955e9b30164" />

   *注：本文档最后更新于 2026-06-21。*
