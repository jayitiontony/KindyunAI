# KindyunAI — Version & Copyright Notice

> KindyunAI Web Service & External Interface Module v1.0.0

---

## 1. Version Information

| Field        | Value                        |
|--------------|------------------------------|
| Module       | Web Service & External API   |
| Version      | **1.0.0**                    |
| Release Date | 2026-06-21                   |
| Status       | Stable                        |
| Build Target | Windows x86_64 (MinGW-w64 GCC 14.2) |
| Runtime Deps | libcurl-x64, libstdc++-6, libgcc_s_seh-1, libwinpthread-1 |

## 2. Copyright

```
Copyright (c) 2026 Kindyun.com. All rights reserved.
```

## 3. Author & Contact

|        |                                          |
|--------|------------------------------------------|
| Website | **https://Kindyun.com**                 |
| Author  | **jayition**                            |
| Email   | **jayition@qq.com**                     |
| Project | KindyunAI Local AI Assistant            |

## 4. License

This module is part of the **KindyunAI** project.

- **License type**: Proprietary / All rights reserved
- **Permitted**: Use, study, modify, and distribute within the same project
- **Prohibited**:
  - Unauthorized copying or redistribution outside the project
  - Reverse engineering for competitive purposes
  - Removing or modifying copyright notices

For licensing inquiries, contact: **jayition@qq.com**

## 5. Module Composition

| Component | Path | Description |
|-----------|------|-------------|
| HTTP Server | `include/kindyun/HttpServer.hpp`<br>`src/server/HttpServer.cpp` | cpp-httplib based HTTP server |
| Session Manager | `include/kindyun/SessionManager.hpp`<br>`src/server/SessionManager.cpp` | Multi-session isolation |
| External Service | `include/kindyun/ExternalService.hpp`<br>`src/server/ExternalService.cpp` | External service adapters |
| API Gateway | `src/server/ApiGateway.hpp`<br>`src/server/ApiGateway.cpp` | Auth + rate-limit + CORS |
| API Handlers | `src/server/ApiHandlers.hpp`<br>`src/server/ApiHandlers.cpp` | REST + UI routes |
| Services | `src/server/services/` | echo / weather / http-forward |
| Web UI | `web/templates/index.html`<br>`web/static/` | Browser frontend |
| Configuration | `config/config.json` (web section) | Web service config |
| Documentation | `docs/WEB_SERVICE_DESIGN.md`<br>`docs/WEB_SERVICE_USAGE.md` | Design + usage docs |

## 6. Runtime Identification

When the program starts, it prints the copyright banner:

```
========================================
     KindyunAI Agent v1.0.0
     Local AI Assistant (C++17)
========================================
  (c) 2026 Kindyun.com
  Website: https://Kindyun.com
  Author : jayition <jayition@qq.com>
----------------------------------------
```

## 7. Changelog

### v1.0.0 (2026-06-21) — Initial Release

**Features**
- Single-process integration: `KindyunAI.exe` runs CLI REPL + Web service + External API in one process
- Web UI (dark theme, single-page, vanilla JS)
- HTTP REST API:
  - `GET /api/v1/health` — health check
  - `POST/GET/DELETE /api/v1/sessions` — session management
  - `POST /api/v1/chat` — synchronous chat
  - `POST /api/v1/chat/stream` — Server-Sent Events streaming chat
  - `GET /api/v1/tools` — list tools
  - `POST /api/v1/tools/{name}/invoke` — direct tool invocation
  - `POST /api/v1/external/{service}` — external service adapter
- Streaming support:
  - Reasoning model (Qwen3.6 / DeepSeek-R1) `reasoning_content` field
  - Tool call streaming (`delta.tool_calls` accumulation)
  - SSE events: `meta`, `delta`, `tool`, `tool_result`, `done`, `error`
- External services (3 built-in):
  - `echo` — request echo (debug)
  - `weather` — wttr.in weather lookup
  - `http` — generic HTTP forward (with host allowlist + SSRF protection)
- Security:
  - Bearer token auth (loopback = auto-allow, LAN = required)
  - CORS
  - Rate limiting (default 120 req/min/token)
  - Host allowlist for `http` service
  - Blocked paths (`/etc`, `C:\Windows`, etc.)
- Configuration via `config/config.json` (web section)
- Vendor: cpp-httplib v0.28 (vendored at `cpp-httplib/httplib.h`)

**Bug Fixes in v1.0.0**
- Fixed CLI REPL exit after `Ctrl+C` (replaced dangling `this` with `shared_ptr`)
- Fixed `cpp-httplib` Windows version requirement (`_WIN32_WINNT=0x0A00`)
- Fixed `/static/*` route (switched to regex `R"(/static/(.*))"`)
- Fixed SSE for reasoning models (read `delta.reasoning_content`)
- Fixed stream tool-call accumulation (parse `delta.tool_calls` by `index`)
- Fixed token auth for loopback host (dev mode auto-skip)

---

## 8. Acknowledgments

This module uses the following open-source components:

| Component | License | Source |
|-----------|---------|--------|
| cpp-httplib v0.28 | MIT | https://github.com/yhirose/cpp-httplib |
| nlohmann/json | MIT | https://github.com/nlohmann/json |
| libcurl | MIT-style | https://curl.se/ |
| SQLite | Public Domain | https://sqlite.org/ |

---

*Document maintained by KindyunAI project. Last updated: 2026-06-21*
*Copyright (c) 2026 Kindyun.com — All rights reserved.*