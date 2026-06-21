# KindyunPluginWebBrowser

## 概述

KindyunAI Web Browser 插件是一个示例工具插件，实现了网页访问功能。通过 libcurl 库获取网页内容，移除 HTML 标签后返回纯文本，供 AI 模型理解和响应。

## 功能特性

- **HTTP GET 请求**：使用 libcurl 库发送 HTTP GET 请求
- **HTML 清理**：移除 HTML 标签，提取纯文本内容
- **重定向支持**：自动跟随重定向（可配置）
- **超时控制**：可配置请求超时时间
- **内容长度限制**：限制返回内容长度，避免超出上下文窗口
- **自定义 User-Agent**：支持设置自定义 User-Agent

## 文件结构

```
KindyunPluginWebBrowser/
├── KindyunPluginWebBrowser.hpp    # 插件头文件
├── KindyunPluginWebBrowser.cpp    # 插件实现文件
├── KindyunPluginWebBrowser.cbp    # Code::Blocks 项目文件
└── README.md                      # 本文件
```

## 构建要求

- **Code::Blocks IDE**（或其他 MinGW GCC 编译器）
- **libcurl**：必须安装 libcurl 开发库（头文件和库文件）
- **C++17** 编译器

## 构建步骤

### 方式一：使用 Code::Blocks IDE

1. 打开 `KindyunPluginWebBrowser.cbp`
2. 确保 libcurl 库路径正确（默认路径：`../../../curl-8.19.0/lib`）
3. 构建目标选择 `Release`
4. 编译输出：`bin/Release/KindyunPluginWebBrowser.dll`

### 方式二：手动编译（MinGW g++）

```bash
cd D:/AIDevelop/KindyunAI/plugins/KindyunPluginWebBrowser/
"C:/Program Files/CodeBlocks/MinGW/bin/g++.exe" -shared -O2 -std=c++17 -DKINDYUN_PLUGIN_EXPORTS \
    -I../../include -I../.. -I../../../curl-8.19.0/include \
    -o bin/Release/KindyunPluginWebBrowser.dll KindyunPluginWebBrowser.cpp \
    -L../../../curl-8.19.0/lib -lcurl -lwtsapi32 -s
```

注意：
- 确保 MinGW g++ 路径正确（默认在 Code::Blocks 安装目录）
- curl 库路径可能需要根据实际位置调整

## 部署步骤

1. 将生成的 `KindyunPluginWebBrowser.dll` 复制到 KindyunAI 主程序目录
2. 确保 libcurl 依赖 DLL（libcurl.dll）在主程序目录或系统路径中
3. 启动 KindyunAI，插件将自动加载

## 工具接口

### 工具名称
`web_browser`

### 工具描述
`web_browser` —— 访问网页并获取文字内容。通过 HTTP GET 请求获取指定 URL 的网页内容，移除 HTML 标签后返回纯文本。适用于获取网页信息、阅读文章、查询在线数据等场景。

### 参数定义

```json
{
    "type": "object",
    "properties": {
        "url": {
            "type": "string",
            "description": "网页 URL"
        },
        "timeout": {
            "type": "integer",
            "description": "超时秒数",
            "default": 15
        },
        "max_length": {
            "type": "integer",
            "description": "最大返回内容长度",
            "default": 5000
        },
        "user_agent": {
            "type": "string",
            "description": "User-Agent 字符串"
        }
    },
    "required": ["url"]
}
```

### 调用示例

当 AI 模型需要调用此工具时，会生成如下参数：

```json
{
    "url": "https://example.com",
    "timeout": 10,
    "max_length": 2000
}
```

### 返回值

**成功时**：
```
URL: https://example.com

[网页纯文本内容]
```

**失败时**：
```
Error: curl request failed (Could not resolve host)
```

## 配置参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| timeout_seconds | int | 15 | 请求超时时间（秒） |
| max_content_length | int | 5000 | 最大返回内容长度（字符数） |
| user_agent | string | "KindyunAI/1.0 (Plugin/WebBrowser)" | User-Agent 字符串 |
| follow_redirects | bool | true | 是否跟随重定向 |
| max_redirects | int | 5 | 最大重定向次数 |

## 注意事项

1. **libcurl 依赖**：确保 libcurl.dll 在主程序目录或系统路径中
2. **SSL 证书**：HTTPS 网站需要 ca-bundle.crt 证书文件
3. **内容长度限制**：默认限制 5000 字符，避免超出上下文窗口
4. **HTML 清理**：简单的 HTML 标签移除，复杂 HTML 可能需要更复杂的解析器
5. **线程安全**：插件当前不是线程安全的，适合单线程使用场景

## 插件接口说明

此插件遵循 KindyunAI 插件接口规范，导出以下函数：

| 函数名 | 说明 |
|--------|------|
| `get_plugin_name()` | 返回插件名称 "WebBrowser" |
| `get_tool_name()` | 返回工具名称 "web_browser" |
| `get_tool_description()` | 返回工具描述 |
| `get_tool_parameters()` | 返回 JSON Schema 参数定义 |
| `execute_tool(arguments)` | 执行网页访问，返回内容 |
| `cleanup_tool()` | 清理插件资源 |

## 许可证

MIT License
