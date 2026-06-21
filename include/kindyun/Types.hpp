/**
 * @file Types.hpp
 * @brief KindyunAI Agent 核心数据类型定义
 *
 * 本文件定义了 AI Agent 系统所有核心数据结构，包括：
 * - 对话消息（Message）：遵循 OpenAI Chat Completion API 的消息格式
 * - 工具调用（ToolCall）：LLM 发起的工具请求
 * - LLM 响应（LLMResponse）：解析后的模型回复
 * - 工具定义（ToolDefinition）：工具的元信息/Schema，用于 function calling
 * - 工具结果（ToolResult）：工具执行完毕后的返回内容
 *
 * 设计原则：
 *   - 所有结构体均为 POD（Plain Old Data），便于 JSON 序列化/反序列化
 *   - 使用 std::optional 表示可选字段，避免歧义
 *   - 不引用其他项目头文件，形成完全独立的数据类型层
 *
 * @note 本文件是项目的"数据契约"层，所有模块通过此处定义的结构体交互。
 *       如需添加新的消息类型或工具字段，请在此处扩展。
 */

#pragma once
#include <string>
#include <vector>
#include <optional>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// ============================================================================
// 对话消息结构
// ============================================================================

/**
 * @brief 对话消息结构体
 *
 * 对应 OpenAI Chat API 中 messages 数组的单个消息对象。
 * 在多轮对话中，所有消息累积形成完整的对话上下文（context）。
 *
 * @note role 字段说明：
 *   - "system"    ：系统级提示词（system prompt），用于设定 AI 的角色和行为准则，通常只在对话开始时出现一次。
 *   - "user"       ：用户输入的原始文本或请求。
 *   - "assistant"  ：LLM（大语言模型）生成的回复，可能包含普通文本或 tool_calls。
 *   - "tool"       ：工具执行的结果反馈，由系统自动注入到消息历史中，role 固定为 "tool"。
 *
 * @note 对于 role="tool" 的消息，tool_call_id 和 name 字段为必填项，用于将工具结果与对应的工具调用匹配。
 */
struct Message {
    /// 消息角色标识："system" | "user" | "assistant" | "tool"
    /// 该字段决定消息在对话中的作用和解析方式。
    std::string role;

    /// 消息的具体文本内容。根据 role 的不同，含义也不同：
    /// - user: 用户的原始输入
    /// - assistant: AI 的回复文本或思考过程
    /// - tool: 工具执行的结果输出
    std::string content;

    /**
     * @brief 【role="tool" 专用】工具调用的唯一 ID
     *
     * 当 LLM 返回 tool_calls 时，每个调用都有一个唯一 ID（如 "call_abc123"）。
     * 工具执行完毕后，必须回传相同的 tool_call_id，LLM 才能将结果与调用关联起来。
     */
    std::optional<std::string> tool_call_id;

    /**
     * @brief 【role="tool" 专用】被调用工具的名称
     *
     * 用于标识该工具结果属于哪个工具，在需要追踪工具调用链时非常有用。
     */
    std::optional<std::string> name;
};

// ============================================================================
// 工具调用结构
// ============================================================================

/**
 * @brief 单个工具调用请求
 *
 * 当 LLM 决定调用工具时，会在响应的 tool_calls 数组中包含此类对象。
 * 每个 ToolCall 代表一个待执行的具体操作（如读文件、执行命令等）。
 *
 * @note arguments 字段已经是解析后的 JSON 对象，
 *       无需再对 function.arguments 字符串调用 json::parse。
 *       但使用者仍需验证其内部字段是否符合工具定义的 schema。
 */
struct ToolCall {
    /// 工具调用的唯一标识符（如 "call_abc123"），由 LLM 生成。
    /// 后续回传 tool 角色消息时必须携带此 ID，以便 LLM 将结果与调用关联。
    std::string id;

    /// 要调用的工具名称（如 "read_file"、"terminal"）。
    /// 此名称必须与 ToolRegistry 中注册的工具名称完全匹配。
    std::string name;

    /**
     * @brief 工具参数（解析后的 JSON 对象）
     *
     * 格式遵循工具定义中 parameters 字段的 schema。
     * 例如对于 read_file 工具，arguments 可能为：{"path": "/tmp/test.txt", "offset": 0, "limit": 1000}
     */
    json arguments;
};

// ============================================================================
// LLM 响应结构
// ============================================================================

/**
 * @brief LLM 响应解析结果
 *
 * 封装从 LLM Chat API 接收到的原始 JSON 响应，并解析为结构化数据。
 * 此结构体是整个 Agent 系统的核心数据载体，贯穿整个对话循环。
 *
 * @note 典型响应场景：
 *   1. LLM 直接回复文本 → has_tool_calls=false, content 非空
 *   2. LLM 决定调用工具 → has_tool_calls=true, tool_calls 非空
 *   3. LLM 出错或超时 → content 可能包含 "Error: ..." 前缀
 *
 * @see LLMClient::parseResponse() 了解具体的解析逻辑
 */
struct LLMResponse {
    /// LLM 返回的文本内容。当 has_tool_calls=true 时，可能包含简要说明或空字符串。
    std::string content;

    /// LLM 请求的工具调用列表。当 has_tool_calls=false 时为空。
    /// 每个元素代表一个需要执行的具体工具操作。
    std::vector<ToolCall> tool_calls;

    /// 标记是否包含工具调用（has_tool_calls=true → tool_calls 非空）。
    /// ConversationLoop 使用此字段判断是返回文本还是进入工具执行循环。
    bool has_tool_calls = false;
};

// ============================================================================
// 工具定义结构（function calling schema）
// ============================================================================

/**
 * @brief 工具定义结构（Function Calling Schema）
 *
 * 此结构体描述一个工具的元信息，用于两个目的：
 *   1. 注册到 ToolRegistry（内部注册）
 *   2. 序列化为 JSON 后发送给 LLM，让模型了解"有什么工具可用"以及"如何调用它们"
 *
 * parameters 字段采用 JSON Schema 格式（与 OpenAI function calling 兼容），
 * 包含参数的类型、描述、必填项等信息，使 LLM 能正确构建参数对象。
 *
 * @note description 字段至关重要 —— LLM 根据此描述决定是否调用该工具。
 *       描述应简洁明了，准确反映工具的用途和约束。
 *
 * @see ToolRegistry::registerTool() 了解工具的注册过程
 */
struct ToolDefinition {
    /// 工具的唯一定位符（如 "read_file"、"terminal"）
    /// 在整个系统中必须全局唯一，ToolRegistry 以名称作为 key 存储。
    std::string name;

    /**
     * @brief 工具功能描述（面向 LLM 的说明）
     *
     * 此描述会直接发送给 LLM，帮助模型判断在什么场景下应该调用此工具。
     * 描述应清晰、准确，包含工具的主要用途和关键行为。
     *
     * 示例：
     *   "Read content of a file. Supports offset and limit for pagination."
     *   "Execute a shell command and return stdout and stderr."
     */
    std::string description;

    /**
     * @brief 参数 Schema（JSON Schema 格式）
     *
     * 描述工具的输入参数，包括类型、必填项、枚举值等约束。
     * 格式需与 OpenAI function calling 的 parameters 字段兼容。
     *
     * 示例：
     * {
     *   "type": "object",
     *   "properties": {
     *     "path": { "type": "string", "description": "文件路径" },
     *     "limit": { "type": "integer", "description": "最大读取行数" }
     *   },
     *   "required": ["path"]
     * }
     */
    json parameters;

    /// 所属工具集名称（如 "core"、"edit"、"terminal"、"search"）
    /// 用于按组启用/禁用工具。Config 中的 enabled_toolsets 控制哪些工具集可用。
    std::string toolset = "core";

    /**
     * @brief 是否需要用户确认后才能执行
     *
     * 对于可能产生副作用的操作（如写入文件、执行命令、删除文件），
     * 设为 true 后会在执行前请求用户确认（由 ApprovalManager 处理）。
     */
    bool requires_approval = false;
};

// ============================================================================
// 工具执行结果
// ============================================================================

/**
 * @brief 工具执行结果
 *
 * 工具执行完毕后，由 ToolDispatcher 封装此结果结构体，
 * 然后通过 ConversationLoop 注入到消息历史中（role="tool"），
 * 最终反馈给 LLM 以继续推理。
 *
 * @note content 字段应包含人类可读的执行结果。
 *       如果 is_error=true，content 应包含错误描述信息。
 */
struct ToolResult {
    /// 对应的 ToolCall::id，用于 LLM 将结果与原始调用关联
    std::string tool_call_id;

    /// 工具执行结果的文本内容，将作为 tool role 消息的内容返回给 LLM
    std::string content;

    /// 标记执行是否出错。true 时，ConversationLoop 会将此消息标注为错误状态
    bool is_error = false;
};