/**
 * @file ConversationLoop.cpp
 * @brief 对话循环核心实现 —— Agent 的决策引擎
 *
 * ConversationLoop 是整个 Agent 系统最核心的组件，负责管理完整的对话生命周期：
 *   1. 用户输入 → 构建消息上下文 → 调用 LLM 获取响应
 *   2. 检测 LLM 响应中的工具调用 → 执行工具 → 将结果回传 LLM
 *   3. 重复步骤 2，直到 LLM 返回最终文本响应
 *   4. 上下文压缩：当对话过长时自动滑动窗口压缩，防止上下文溢出
 *
 * 核心循环（ReAct 范式）：
 *   User Input → [LLM] → Tool Call(s) → [Execute Tool(s)] → [LLM] → ... → Final Response
 *
 * 关键特性：
 *   - 自动重试：LLM 空响应时自动重试（最多 3 次）
 *   - 连续失败检测：连续 3 次 LLM 失败后提示用户检查配置
 *   - 上下文溢出自动压缩：LLM ContextOverflow 时自动滑动窗口压缩
 *   - 工具审批：支持在危险工具执行前请求用户确认
 *   - 会话持久化：支持保存/加载完整对话历史
 *   - 流式输出：支持逐字打印 LLM 响应
 *
 * @note 本文件使用 TokenEstimator 对对话 token 数进行估算，以决定是否触发压缩。
 * @see ConversationLoop.hpp 为类接口定义
 * @see LLMClient 为 LLM 通信层
 * @see ToolDispatcher 为工具分发层
 * @note 本文件使用 TokenEstimator 对对话 token 数进行估算，以决定是否触发压缩。
 * @see ConversationLoop.hpp 为类接口定义
 * @see LLMClient 为 LLM 通信层
 * @see ToolDispatcher 为工具分发层
 */
#include "kindyun/ConversationLoop.hpp"
#include "kindyun/LLMClient.hpp"
#include "kindyun/ToolDispatcher.hpp"
#include "kindyun/ToolBase.hpp"
#include "kindyun/ApprovalManager.hpp"
#include "kindyun/MemoryDB.hpp"
#include "kindyun/TokenEstimator.hpp"
#include "kindyun/KindyunGlobal.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

/**
 * @brief 连续 LLM 失败阈值
 *
 * 当 LLM 连续返回错误（或空响应）达到此值时，ConversationLoop 会输出警告信息，
 * 提示用户检查配置（如 API Key、base_url、网络等）。
 *
 * @note 此值设为 3 次，避免单次网络抖动导致误判。
 */
static const int CONSECUTIVE_LLM_FAILURES_THRESHOLD = 3;

/**
 * @brief 构造函数 —— 初始化对话循环组件
 *
 * 从 Config 单例读取最大迭代次数和空响应重试次数。
 * 重置连续失败计数。
 *
 * @param llm LLM 通信客户端引用（用于发送请求和接收响应）
 * @param dispatcher 工具分发器引用（用于执行工具调用）
 * @param config 配置管理器引用（用于读取系统配置）
 *
 * @note 不持有 Config 的拥有权（非 owner），Config 生命周期由 main() 管理。
 */
ConversationLoop::ConversationLoop(
    LLMClient& llm,
    ToolDispatcher& dispatcher,
    const Config& config
) : m_llm(llm), m_dispatcher(dispatcher), m_config(config) {
    m_maxIterations = config.max_iterations();
    m_emptyResponseMaxRetries = config.empty_response_retries();
    m_consecutive_llm_failures = 0;
}

/**
 * @brief 非流式运行单轮对话（同步模式）
 *
 * 这是 Agent 的核心入口函数。完整的工作流程：
 *   1. 将用户输入添加到消息历史
 *   2. 自检：清理可能损坏的历史状态
 *   3. 根据配置加载已启用的工具集
 *   4. 构建消息上下文（system prompt + history）
 *   5. 调用 LLM 获取响应
 *   6. 如果 LLM 返回工具调用，进入工具执行循环
 *   7. 执行每个工具，将结果回传给 LLM
 *   8. 重复步骤 5-7 直到 LLM 返回最终文本响应
 *   9. 检查是否需要上下文压缩
 *
 * @param userInput 用户的自然语言输入
 * @return LLM 的最终文本响应（包含所有工具执行结果的综合回复）
 *
 * @note 此函数为阻塞调用，在工具执行期间不会返回。
 * @see executeToolLoop() 为工具执行循环的具体实现
 * @see runStream() 为对应的流式版本
 */
std::string ConversationLoop::run(const std::string& userInput) {
    if (userInput.empty()) return "";

    m_emptyResponseRetries = 0;
    m_iterationCount = 0;

    // 将用户输入添加到消息历史（role="user"）
    m_history.push_back(Message{"user", userInput});

    // 自检：清理损坏的 history 状态
    // 如果 history 最后一条是 tool 消息，但倒数第二条不是带 tool_call_id 的 assistant → 状态损坏
    if (m_history.size() >= 2) {
        const auto& last = m_history[m_history.size() - 1];
        const auto& prev = m_history[m_history.size() - 2];
        if (last.role == "tool" &&
            !(prev.role == "assistant" && prev.tool_call_id.has_value())) {
            std::cerr << "[ConversationLoop] Recovered from corrupted history, removing stray tool message." << std::endl;
            m_history.pop_back();
        }
    }

    // 根据配置加载已启用的工具集（从 ToolRegistry 获取各工具集的定义）
    auto enabled = m_config.enabled_toolsets();
    std::vector<ToolDefinition> tools;
    for (const auto& ts : enabled) {
        auto ts_tools = ToolRegistry::instance().getToolDefinitionsInToolset(ts);
        tools.insert(tools.end(), ts_tools.begin(), ts_tools.end());
    }

    // 进入工具执行循环
    return executeToolLoop(tools);
}

/**
 * @brief 流式运行单轮对话（实时输出模式）
 *
 * 与 run() 类似，但在 LLM 响应时通过回调函数实时输出每个 token chunk，
 * 实现逐字打字效果。流式模式下工具调用支持简化（如需完整支持可回退到非流式）。
 *
 * @param userInput 用户的自然语言输入
 * @param outputCallback 可选的输出回调函数，每个 token chunk 都会触发此回调。
 *                       可用于实时显示到终端 UI。
 * @return LLM 的完整响应文本（累积所有 chunk 后的结果）
 *
 * @note 流式模式下，LLM 通过 LLMClient::generateStream() 逐 chunk 返回。
 * @see run() 为非流式对应版本
 */
std::string ConversationLoop::runStream(const std::string& userInput,
                                        StreamEventCallback outputCallback) {
    if (userInput.empty()) return "";

    m_emptyResponseRetries = 0;
    m_iterationCount = 0;

    // 将用户输入添加到消息历史
    m_history.push_back(Message{"user", userInput});

    // 根据配置加载已启用的工具集
    auto enabled = m_config.enabled_toolsets();
    std::vector<ToolDefinition> tools;
    for (const auto& ts : enabled) {
        auto ts_tools = ToolRegistry::instance().getToolDefinitionsInToolset(ts);
        tools.insert(tools.end(), ts_tools.begin(), ts_tools.end());
    }

    return executeToolLoopStream(tools, outputCallback);
}

/**
 * @brief 核心工具执行循环 —— ReAct 范式的实现
 *
 * 这是整个 Agent 最核心的方法。它实现了一个完整的 ReAct (Reasoning + Acting) 循环：
 *   1. 构建消息上下文（system prompt + history）
 *   2. 调用 LLM 获取响应
 *   3. 检测响应类型：
 *      - 上下文溢出 (ContextOverflow) → 自动压缩后重试
 *      - 错误 → 累计连续失败次数
 *      - 空响应 → 自动重试（最多 m_emptyResponseMaxRetries 次）
 *      - 工具调用 → 执行每个工具并将结果回传 LLM
 *      - 纯文本响应 → 循环结束，返回最终结果
 *   4. 循环直到返回最终文本响应
 *
 * 上下文压缩策略：
 *   - 当 LLM 返回 ContextOverflow 时，自动调用 compressContext()
 *   - compressContext() 使用滑动窗口策略保留最近的消息
 *
 * @param tools 已启用工具的完整定义列表（已按 enabled_toolsets 过滤）
 * @return LLM 的最终文本响应
 *
 * @note 此函数为递归自调用：空响应时会重新调用 executeToolLoop() 自身进行重试。
 * @note 工具调用期间，m_iterationCount 递增，超过 m_maxIterations 时终止循环。
 */
std::string ConversationLoop::executeToolLoop(const std::vector<ToolDefinition>& tools) {
    // ========== Step 1: 构建消息上下文 ==========

    // 构建消息列表：先加入 system prompt（如果有）
    std::vector<Message> msgs;
    if (!m_systemPrompt.empty()) {
        msgs.push_back(Message{"system", m_systemPrompt});
    }

    // 添加完整的历史消息（包括之前的 user/assistant/tool 消息）
    for (const auto& msg : m_history) {
        msgs.push_back(msg);
    }

    // ========== Step 2: 调用 LLM ==========
    auto resp = m_llm.chat(msgs, tools);
    if (m_config.debug_conversation()) {
        logConversationDebug(msgs, resp.content);
    }


    // ========== Step 3: 检查 LLM 响应状态 ==========

    // 3a. 上下文溢出检测：LLM 返回 ContextOverflow 错误
    //     策略：自动压缩上下文后重试一次
    auto llmError = m_llm.getLastError();
    if (llmError.type == LLMErrorType::ContextOverflow) {
        std::cerr << "[ConversationLoop] Context overflow, compressing history and retrying..." << std::endl;
        compressContext();
        // 压缩后重新构建消息并调用 LLM
        resp = m_llm.chat(msgs, tools);
    if (m_config.debug_conversation()) {
        logConversationDebug(msgs, resp.content);
    }
        llmError = m_llm.getLastError();
        if (llmError.type == LLMErrorType::ContextOverflow) {
            // 压缩后仍然溢出：说明模型上下文窗口太小，无法处理此对话
            std::cerr << "[ConversationLoop] Context still overflows after compression. "
                      << "Your model's context window may be too small for this conversation." << std::endl;
        }
    }

    // 3b. 错误检测：LLM 返回错误或者内容以 "Error:" 开头
    //     累计连续失败次数，超过阈值时提示用户
    if (llmError.type != LLMErrorType::Unknown || resp.content.find("Error:") == 0) {
        m_consecutive_llm_failures++;
        if (m_consecutive_llm_failures >= CONSECUTIVE_LLM_FAILURES_THRESHOLD) {
            std::cerr << "[ConversationLoop] Warning: " << m_consecutive_llm_failures
                      << " consecutive LLM failures. Please check your configuration." << std::endl;
        }
    } else {
        m_consecutive_llm_failures = 0;
    }

    // 3c. 空响应检测：LLM 返回了空内容且没有工具调用
    //     策略：自动重试（最多 m_emptyResponseMaxRetries 次）
    if (resp.content.empty() && !resp.has_tool_calls) {
        m_emptyResponseRetries++;
        if (m_emptyResponseRetries <= m_emptyResponseMaxRetries) {
            return executeToolLoop(tools);
        }
        return "Error: LLM returned empty response after " +
               std::to_string(m_emptyResponseMaxRetries) + " retries.";
    }
    m_emptyResponseRetries = 0;

    // ========== Step 4: 将助手回复添加到历史 ==========
    Message assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = resp.content;
    m_history.push_back(assistant_msg);

    // ========== Step 5: 处理工具调用（ReAct 核心循环）==========

    // 只要 LLM 返回了工具调用，就持续执行循环
    while (resp.has_tool_calls) {
        m_iterationCount++;
        if (m_iterationCount > m_maxIterations) {
            return resp.content + "\n[Error: max iterations reached, stopping tool loop]";
        }

        // 执行每个工具调用（异常保护：确保单个工具崩溃不影响整个对话）
        for (const auto& tc : resp.tool_calls) {
            ToolResult tr;
            try {
                // 通过 handleToolCall 执行工具（包含审批检查、记忆记录）
                tr = handleToolCall(tc);
            } catch (const std::exception& ex) {
                tr.tool_call_id = tc.id;
                tr.content = std::string("[Tool Exception] ") + ex.what();
                tr.is_error = true;
            } catch (...) {
                tr.tool_call_id = tc.id;
                tr.content = "[Tool Exception] unknown error";
                tr.is_error = true;
            }

            // 将工具结果作为 tool role 消息添加到历史
            Message tool_msg;
            tool_msg.role = "tool";
            tool_msg.tool_call_id = tc.id;
            tool_msg.name = tc.name;
            tool_msg.content = tr.content;
            m_history.push_back(tool_msg);
        }

        // ========== Step 6: 将工具结果回传给 LLM 继续推理 ==========

        // 重新构建完整消息上下文
        try {
            msgs.clear();
            if (!m_systemPrompt.empty()) {
                msgs.push_back(Message{"system", m_systemPrompt});
            }
            for (const auto& msg : m_history) {
                msgs.push_back(msg);
            }
            // 调用 LLM，传入工具执行结果
            resp = m_llm.chat(msgs, tools);
    if (m_config.debug_conversation()) {
        logConversationDebug(msgs, resp.content);
    }


        } catch (const std::exception& ex) {
            m_history.push_back(Message{"assistant",
                std::string("[LLM Exception] ") + ex.what()});
            return "[Error: LLM call failed] " + std::string(ex.what());
        } catch (...) {
            m_history.push_back(Message{"assistant", "[LLM Exception] unknown error"});
            return "[Error: LLM call failed]";
        }

        // ========== Step 7: 再次检查 LLM 响应状态（与 Step 3 相同逻辑）==========

        // 工具循环中的上下文溢出检测
        auto loopError = m_llm.getLastError();
        if (loopError.type == LLMErrorType::ContextOverflow) {
            std::cerr << "[ConversationLoop] Context overflow in tool loop, compressing..." << std::endl;
            compressContext();
            // 重新构建消息后重试一次
            try {
                msgs.clear();
                if (!m_systemPrompt.empty()) msgs.push_back(Message{"system", m_systemPrompt});
                for (const auto& msg : m_history) msgs.push_back(msg);
                resp = m_llm.chat(msgs, tools);
    if (m_config.debug_conversation()) {
        logConversationDebug(msgs, resp.content);
    }

            } catch (const std::exception& ex) {
                m_history.push_back(Message{"assistant", std::string("[LLM Exception] ") + ex.what()});
                return "[Error: LLM call failed] " + std::string(ex.what());
            } catch (...) {
                m_history.push_back(Message{"assistant", "[LLM Exception] unknown error"});
                return "[Error: LLM call failed]";
            }
            loopError = m_llm.getLastError();
        }

        // 工具循环中的错误检测
        if (loopError.type != LLMErrorType::Unknown || resp.content.find("Error:") == 0) {
            m_consecutive_llm_failures++;
            if (m_consecutive_llm_failures >= CONSECUTIVE_LLM_FAILURES_THRESHOLD) {
                std::cerr << "[ConversationLoop] Warning: " << m_consecutive_llm_failures
                          << " consecutive LLM failures. Please check your configuration." << std::endl;
            }
        } else {
            m_consecutive_llm_failures = 0;
        }

        // 工具循环中的空响应检测
        if (resp.content.empty() && !resp.has_tool_calls) {
            m_emptyResponseRetries++;
            if (m_emptyResponseRetries <= m_emptyResponseMaxRetries) {
                continue;
            }
            return "[Error: empty response in tool loop]";
        }
        m_emptyResponseRetries = 0;

        // 将 LLM 的中间回复添加到历史
        m_history.push_back(Message{"assistant", resp.content});
    }

    // ========== Step 8: 检查是否需要上下文压缩 ==========
    // 对话结束后，检查上下文大小是否超过滑动窗口目标
    if (shouldCompress()) {
        compressContext();
    }

    return resp.content;
}

/**
 * @brief 流式工具执行循环 —— 实时输出模式的核心实现
 *
 * 与 executeToolLoop() 类似，但通过 LLMClient::generateStream() 实现流式输出：
 *   1. LLM 每返回一个 chunk，立即通过 outputCallback 回调输出
 *   2. 同时累积到 full_content 缓冲区，用于最终返回
 *   3. 工具调用支持简化（如需完整支持可回退到非流式）
 *
 * @param tools 已启用工具的完整定义列表
 * @param outputCallback 实时输出回调函数，每个 token chunk 都会触发此回调。
 *                       可用于实时显示到终端 UI。
 * @return LLM 的完整响应文本（累积所有 chunk 后的结果）
 *
 * @note 流式模式下，LLM 通过 LLMClient::generateStream() 逐 chunk 返回。
 * @see run() 为非流式对应版本
 */
std::string ConversationLoop::executeToolLoopStream(
    const std::vector<ToolDefinition>& tools,
    StreamEventCallback outputCallback
) {
    // 构建消息上下文（system prompt + history）
    std::vector<Message> msgs;
    if (!m_systemPrompt.empty()) {
        msgs.push_back(Message{"system", m_systemPrompt});
    }
    for (const auto& msg : m_history) {
        msgs.push_back(msg);
    }

    // 用于累积完整响应的 buffer（最终返回用）
    std::string full_content;

    // 辅助 lambda：推 Text 事件（不中止）
    auto pushText = [&](const std::string& chunk) {
        full_content += chunk;
        if (outputCallback) {
            StreamEvent ev;
            ev.type = StreamEvent::Text;
            ev.text = chunk;
            outputCallback(ev);
        }
    };

    // 通过 LLM 的流式接口发起请求，每收到一个 chunk 就触发 Text 事件
    auto resp = m_llm.generateStream(msgs, tools, [&](const std::string& chunk) -> bool {
        pushText(chunk);
        return true;
    });

    // 流式上下文溢出检测
    auto llmError = m_llm.getLastError();
    if (llmError.type == LLMErrorType::ContextOverflow) {
        std::cerr << "[ConversationLoop] Context overflow in stream, compressing..." << std::endl;
        compressContext();
        msgs.clear();
        if (!m_systemPrompt.empty()) msgs.push_back(Message{"system", m_systemPrompt});
        for (const auto& msg : m_history) msgs.push_back(msg);
        resp = m_llm.chat(msgs, tools);
        if (m_config.debug_conversation()) {
            logConversationDebug(msgs, resp.content);
        }
        full_content = resp.content;
        if (outputCallback) {
            StreamEvent ev;
            ev.type = StreamEvent::Text;
            ev.text = resp.content;
            outputCallback(ev);
        }
        llmError = m_llm.getLastError();
    }

    // 错误检测和连续失败计数
    if (llmError.type != LLMErrorType::Unknown || resp.content.find("Error:") == 0) {
        m_consecutive_llm_failures++;
        if (m_consecutive_llm_failures >= CONSECUTIVE_LLM_FAILURES_THRESHOLD) {
            std::cerr << "[ConversationLoop] Warning: " << m_consecutive_llm_failures
                      << " consecutive LLM failures. Please check your configuration." << std::endl;
        }
        if (outputCallback) {
            StreamEvent ev;
            ev.type = StreamEvent::Text;
            ev.text = "\n[Error: " + llmError.message + "]\n";
            outputCallback(ev);
        }
    } else {
        m_consecutive_llm_failures = 0;
    }

    // 工具调用循环：如果 LLM 决定调用工具，依次执行并把结果注入历史，递归
    if (!resp.tool_calls.empty()) {
        std::cerr << "[ConversationLoop] Stream: " << resp.tool_calls.size()
                  << " tool call(s) detected" << std::endl;

        // 推 assistant tool_calls 消息到历史（用 JSON 表示）
        // 为简化，把 content 也保留（可能为空）
        Message assistant_msg;
        assistant_msg.role = "assistant";
        assistant_msg.content = full_content;
        m_history.push_back(assistant_msg);

        // 逐个执行工具
        for (const auto& tc : resp.tool_calls) {
            // 1. 推 ToolPending 事件
            if (outputCallback) {
                StreamEvent ev;
                ev.type = StreamEvent::ToolPending;
                ev.tool_call_id = tc.id;
                ev.tool_name = tc.name;
                ev.tool_args = tc.arguments;
                outputCallback(ev);
            }

            // 2. 执行工具
            ToolResult result = handleToolCall(tc);

            // 3. 推 ToolResult 事件
            if (outputCallback) {
                StreamEvent ev;
                ev.type = StreamEvent::ToolResult;
                ev.tool_call_id = tc.id;
                ev.tool_name = tc.name;
                ev.tool_result = result.content;
                ev.is_error = result.is_error;
                outputCallback(ev);
            }

            // 4. 把 tool role 消息加入历史
            Message tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = result.content;
            tool_msg.tool_call_id = tc.id;
            tool_msg.name = tc.name;
            m_history.push_back(tool_msg);
        }

        // 5. 递归执行下一轮 LLM（可能再调工具或给最终回复）
        return executeToolLoopStream(tools, outputCallback);
    }

    // 空响应检测和重试
    if (resp.content.empty()) {
        m_emptyResponseRetries++;
        if (m_emptyResponseRetries <= m_emptyResponseMaxRetries) {
            return executeToolLoopStream(tools, outputCallback);
        }
        return "Error: LLM returned empty response after retries.";
    }
    m_emptyResponseRetries = 0;

    // 将助手回复添加到消息历史
    Message assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = full_content;
    m_history.push_back(assistant_msg);

    // 检查是否需要上下文压缩
    if (shouldCompress()) {
        compressContext();
    }

    return full_content;
}

/**
 * @brief 处理单个工具调用 —— 包含审批检查和记忆记录
 *
 * 在工具实际执行前，检查是否需要用户审批（Phase 2 功能）：
 *   - 如果 ApprovalManager::needsApproval(tc.name) 返回 true，则调用 approve()
 *   - 如果用户拒绝执行，返回错误消息 [Denied]
 *
 * 工具执行后，无论成功或失败，都将结果记录到 MemoryDB（Phase 3 功能）。
 *
 * @param tc 工具调用信息（包含工具名称、参数等）
 * @return ToolResult 包含工具执行结果、错误标志等
 *
 * @note 审批检查仅当 m_approval_mgr 已设置且对应工具需要审批时执行。
 * @note 记忆记录仅当 m_memory_db 已设置时执行。
 * @see ApprovalManager 为审批管理器
 * @see MemoryDB 为记忆数据库
 */
ToolResult ConversationLoop::handleToolCall(const ToolCall& tc) {
    // Phase 2: 审批检查 —— 检查该工具是否需要用户确认
    if (m_approval_mgr && m_approval_mgr->needsApproval(tc.name)) {
        std::string args_desc = tc.arguments.dump();
        bool approved = m_approval_mgr->approve(tc.name, args_desc);
        if (!approved) {
            // 用户拒绝执行，构造错误结果
            ToolResult r;
            r.tool_call_id = tc.id;
            r.content = "[Denied] User denied execution of: " + tc.name;
            r.is_error = true;
            // 记录拒绝到记忆数据库
            if (m_memory_db) {
                m_memory_db->addMessage("tool_error", r.content, tc.id);
            }
            return r;
        }
    }

    // 通过工具分发器执行工具
    ToolResult result = m_dispatcher.dispatch(tc);

    // Phase 2/3: 记录到记忆数据库（无论成功或失败都记录）
    if (m_memory_db) {
        if (result.is_error) {
            m_memory_db->addMessage("tool_error", result.content, tc.id);
        } else {
            m_memory_db->addMessage("tool", result.content, tc.id);
        }
    }

    return result;
}

/**
 * @brief 计算当前上下文的总 token 数（估算值）
 *
 * 使用 TokenEstimator 对 system prompt 和历史消息的总 token 数进行估算，
 * 用于上下文压缩决策。
 *
 * @return 当前上下文的估算 token 总数
 *
 * @note 使用估算值而非精确计数，以平衡计算开销和准确性。
 * @see TokenEstimator::estimateWithSystem
 */
size_t ConversationLoop::getContextTokenCount() const {
    // 计算当前上下文的总 token 数（包含 system prompt 和历史消息）
    return TokenEstimator::estimateWithSystem(m_systemPrompt, m_history);
}

/**
 * @brief 判断是否需要上下文压缩
 *
 * 当上下文总 token 数超过配置的滑动窗口目标值时，返回 true。
 *
 * @return true 如果当前 token 数超过滑动窗口目标值
 *
 * @note 此方法被 shouldCompress() 调用，进而触发 compressContext()。
 * @see shouldCompress() 为压缩触发条件
 * @see compressContext() 为实际压缩执行
 */
bool ConversationLoop::shouldCompress() const {
    // 当总 token 数超过滑动窗口目标时触发压缩
    size_t windowTokens = m_config.sliding_window_tokens();
    size_t currentTokens = getContextTokenCount();
    return currentTokens > windowTokens;
}

/**
 * @brief 执行上下文压缩 —— 滑动窗口策略实现
 *
 * 核心压缩算法：
 *   1. 保留 system prompt（不可压缩）
 *   2. 保留最近 N 条消息（preserveRecent，不可压缩）
 *   3. 中间旧消息分为两种处理方式：
 *      - 启用摘要（enableSummary=true）：调用 LLM 生成真实摘要
 *      - 禁用摘要（enableSummary=false）或摘要失败：使用占位符 \"[已省略 X 条消息，内容未记录]\"
 *   4. 压缩后总 token 数尽量不超过 sliding_window_tokens
 *
 * 压缩流程：
 *   - 分离 messages：system_msgs / middle_msgs（旧）/ recent_msgs（新）
 *   - 对 middle_msgs 生成摘要或占位符
 *   - 构建新历史：system_msgs + [summary] + recent_msgs
 *   - 验证 token 数，必要时缩减 recent_msgs 直到满足要求
 *
 * @note 此方法被 compressContext() 调用，在 shouldCompress() 返回 true 时执行。
 * @note 摘要生成过程中不带工具，避免递归调用。
 * @note 如果 LLM 摘要生成失败，降级为占位符策略。
 */
void ConversationLoop::compressContext() {
// 滑动窗口压缩策略（Phase 3 升级）：
// 1. 保留 system prompt
    // 2. 保留最近 N 条消息（preserve_recent_messages）
    // 3. 中间旧消息：启用摘要时用 LLM 生成真实摘要，禁用时用占位符
    // 4. 压缩后总 token 数尽量不超过 sliding_window_tokens
    int preserveRecent = m_config.preserve_recent_messages();
    size_t targetTokens = m_config.sliding_window_tokens();
    bool enableSummary = m_config.enable_summarization();

    if ((int)m_history.size() <= preserveRecent) {
        return; // 消息太少，无需压缩
    }

    // 分离 system / 中间旧消息 / 最近消息
    std::vector<Message> system_msgs;
    std::vector<Message> middle_msgs;
    std::vector<Message> recent_msgs;

    for (const auto& msg : m_history) {
        if (msg.role == "system") {
            system_msgs.push_back(msg);
        }
    }

    std::vector<Message> nonSystem_msgs;
    for (const auto& msg : m_history) {
        if (msg.role != "system") {
            nonSystem_msgs.push_back(msg);
        }
    }

    if ((int)nonSystem_msgs.size() <= preserveRecent) {
        return; // 消息太少，无需压缩
    }

    middle_msgs.assign(nonSystem_msgs.begin(),
                       nonSystem_msgs.end() - preserveRecent);
    recent_msgs.assign(nonSystem_msgs.end() - preserveRecent,
                       nonSystem_msgs.end());

    // 构建摘要消息内容
    std::string summaryContent;

    if (enableSummary && !middle_msgs.empty()) {
        // 发送 LLM 请求生成摘要
        std::cerr << "[ConversationLoop] Generating context summary for "
                  << middle_msgs.size() << " old messages..." << std::endl;

        std::ostringstream prompt;
        prompt << "请简洁地总结以下对话的要点，保留关键信息（如任务目标、重要决策、工具调用结果）。"
               << "摘要应控制在 200 字以内，直接输出摘要内容，不需要额外格式：\n\n";

        for (const auto& msg : middle_msgs) {
            prompt << "【" << msg.role << "】" << msg.content << "\n\n";
        }

        // 调用 LLM（不带工具，避免递归）
        Message sum_req;
        sum_req.role = "user";
        sum_req.content = prompt.str();

        std::vector<Message> sum_msgs;
        if (!m_systemPrompt.empty()) {
            sum_msgs.push_back(Message{"system", "你是一个擅长提炼长对话要点的助手。输出简洁的中文摘要即可。"});
        }
        sum_msgs.push_back(sum_req);

        auto sum_resp = m_llm.chat(sum_msgs, {});

        if (sum_resp.content.empty() || sum_resp.content.find("Error:") == 0) {
            std::cerr << "[ConversationLoop] Summarization failed, using placeholder." << std::endl;
            std::ostringstream oss;
            oss << "[已省略 " << middle_msgs.size() << " 条消息，内容未记录]";
            summaryContent = oss.str();
        } else {
            // 去掉可能的引号包装
            std::string raw = sum_resp.content;
            if (!raw.empty() && raw.front() == '\"' && raw.back() == '\"') {
                raw = raw.substr(1, raw.size() - 2);
            }
            if (!raw.empty() && raw.front() == '`' && raw.back() == '`') {
                raw = raw.substr(1, raw.size() - 2);
            }
            std::ostringstream oss;
            oss << "[上文摘要] " << raw;
            summaryContent = oss.str();
        }
    } else {
        // 未启用摘要或没有中间消息
        if (!middle_msgs.empty()) {
            std::ostringstream oss;
            oss << "[已省略 " << middle_msgs.size() << " 条消息，内容未记录]";
            summaryContent = oss.str();
        }
    }

    // 构建压缩后的历史
    std::vector<Message> new_history;

    for (const auto& msg : system_msgs) {
        new_history.push_back(msg);
    }

    if (!summaryContent.empty()) {
        Message sum_msg;
        sum_msg.role = "system";
        sum_msg.content = summaryContent;
        new_history.push_back(sum_msg);
    }

    for (const auto& msg : recent_msgs) {
        new_history.push_back(msg);
    }

    // 验证 token 数，若仍超目标，激进地缩减保留消息数
    size_t newTokens = TokenEstimator::estimateWithSystem(m_systemPrompt, new_history);
    if (newTokens <= targetTokens) {
        m_history = new_history;
        std::cerr << "[ConversationLoop] Context compressed: ~" << newTokens
                  << " tokens (" << middle_msgs.size() << " messages summarized)." << std::endl;
    } else {
        // 逐步减少保留消息数，直到 token 降到目标以下
        while ((int)recent_msgs.size() > 2 && newTokens > targetTokens) {
            recent_msgs.erase(recent_msgs.begin());
            new_history.clear();

            for (const auto& msg : system_msgs) new_history.push_back(msg);
            if (!summaryContent.empty()) {
                new_history.push_back(Message{"system", summaryContent});
            }
            for (const auto& msg : recent_msgs) new_history.push_back(msg);

            newTokens = TokenEstimator::estimateWithSystem(m_systemPrompt, new_history);
        }
        m_history = new_history;
        std::cerr << "[ConversationLoop] Context compressed (aggressive): ~" << newTokens
                  << " tokens." << std::endl;
    }
}

/**
 * @brief 获取消息历史引用（只读）
 * @return 当前对话历史的 const 引用
 */
const std::vector<Message>& ConversationLoop::history() const {
    return m_history;
}

/**
 * @brief 清空消息历史和相关状态
 *
 * 重置：
 *   - m_history（清空消息列表）
 *   - m_iterationCount（迭代计数）
 *   - m_emptyResponseRetries（空响应重试计数）
 *   - m_consecutive_llm_failures（连续失败计数）
 *
 * @note 不重置 m_systemPrompt（系统提示应保持不变）
 * @note 不重置 m_approval_mgr 和 m_memory_db（这些是注入的依赖）
 */
void ConversationLoop::clearHistory() {
    m_history.clear();
    m_iterationCount = 0;
    m_emptyResponseRetries = 0;
    m_consecutive_llm_failures = 0;
}

/**
 * @brief 设置系统提示词
 * @param prompt 新的系统提示内容
 */
void ConversationLoop::setSystemPrompt(const std::string& prompt) {
    m_systemPrompt = prompt;
}

/**
 * @brief 注入审批管理器
 * @param mgr 审批管理器指针（可为 nullptr 表示不启用审批）
 */
void ConversationLoop::setApprovalManager(ApprovalManager* mgr) {
    m_approval_mgr = mgr;
}

/**
 * @brief 注入记忆数据库
 * @param db 记忆数据库指针（可为 nullptr 表示不启用记忆）
 */
void ConversationLoop::setMemoryDB(MemoryDB* db) {
    m_memory_db = db;
}

/**
 * @brief 保存会话到文件
 *
 * 将完整对话历史（包括每条消息的角色、内容、工具调用 ID、名称等）
 * 和 system prompt 序列化到 JSON 文件。
 *
 * @param path 保存路径（JSON 格式）
 *
 * @note 文件格式：{\"messages\": [...], \"system_prompt\": \"...\"}
 * @note 每条消息包含：role, content, tool_call_id（可选）, name（可选）
 * @see loadSession() 为加载对应方法
 */
void ConversationLoop::saveSession(const std::string& path) const {
    json j;
    j["messages"] = json::array();
    for (const auto& msg : m_history) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (msg.tool_call_id.has_value()) m["tool_call_id"] = *msg.tool_call_id;
        if (msg.name.has_value()) m["name"] = *msg.name;
        j["messages"].push_back(m);
    }
    j["system_prompt"] = m_systemPrompt;

    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2);
    }
}

/**
 * @brief 从文件加载会话
 *
 * 从 JSON 文件读取消息列表和 system prompt，恢复对话历史。
 *
 * @param path 会话文件路径（JSON 格式）
 *
 * @note 加载后清空现有历史并替换为新数据
 * @note 如果文件无法打开或格式不正确，静默返回
 * @see saveSession() 为保存对应方法
 */
void ConversationLoop::loadSession(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    json j;
    f >> j;
    m_history.clear();
    if (j.contains("messages")) {
        for (const auto& m : j["messages"]) {
            Message msg;
            msg.role = m["role"].get<std::string>();
            msg.content = m["content"].get<std::string>();
            if (m.contains("tool_call_id")) msg.tool_call_id = m["tool_call_id"].get<std::string>();
            if (m.contains("name")) msg.name = m["name"].get<std::string>();
            m_history.push_back(msg);
        }
    }
    if (j.contains("system_prompt")) {
        m_systemPrompt = j["system_prompt"].get<std::string>();
    }
}

void ConversationLoop::logConversationDebug(const std::vector<Message>& msgs, const std::string& response_content) const {
    if (!m_config.debug_conversation()) return;

    std::cout << "\n================ [DEBUG-CONVERSATION] ================\n";
    std::cout << "--- MESSAGES CONTEXT ---\n";
    for (const auto& m : msgs) {
        std::cout << "[" << convert_to_gbk(m.role) << "] " << convert_to_gbk(m.content) << "\n";
    }
    std::cout << "--- LLM RESPONSE ---\n";
    std::cout << convert_to_gbk(response_content) << "\n";
    std::cout << "=====================================================\n" << std::endl;
}
