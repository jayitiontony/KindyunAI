/**
 * @file LLMClient.cpp
 * @brief LLM HTTP 客户端实现
 *
 * 实现 LLMClient，使用 libcurl 进行 HTTP通信，
 * 解析 OpenAI-compatible /v1/chat/completions 响应。
 * 支持自动重试和错误分类（Phase 3）。
 */

#include "kindyun/LLMClient.hpp"
#include "kindyun/KindyunGlobal.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

using json = nlohmann::json;

LLMClient::LLMClient(const std::string& url, const std::string& model)
    : m_base_url(url), m_model_name(model), m_timeout(120), m_stream_timeout(300) {}

void LLMClient::setApiKey(const std::string& apiKey) {
    m_api_key = apiKey;
}

void LLMClient::setTimeout(int timeout) {
    m_timeout = timeout;
}

void LLMClient::setStreamTimeout(int timeout) {
    m_stream_timeout = timeout;
}

LLMError LLMClient::getLastError() const {
    return m_last_error;
}

int LLMClient::getRetryCount() const {
    return m_retry_count;
}

void LLMClient::clearError() {
    m_last_error = LLMError{};
    m_retry_count = 0;
}

// Callback: curl 将接收到的数据 append 到 std::string
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// ===== 流式响应支持 =====

// 流式回调上下文
struct StreamContext {
    StreamCallback callback;
    std::string buffer;          // 未处理的缓冲数据
    std::string full_content;     // 累积的完整文本（用于最终响应）
    std::vector<ToolCall> tool_calls;       // 累积的 tool_calls（每个 index 一份）
    std::vector<std::string> tool_args_buf;  // 每个 tool_call 对应的累积 arguments 字符串
    bool continue_streaming = true;

    explicit StreamContext(StreamCallback cb) : callback(std::move(cb)) {}

    // 工具调用解析：把每帧的 arguments 字符串累积到 tool_args_buf[index]
    // 首帧覆盖（带 id 字段），后续帧拼接
    void accumulateToolCallChunk(const json& delta) {
        if (!delta.contains("tool_calls") || !delta["tool_calls"].is_array()) return;
        for (const auto& dtc : delta["tool_calls"]) {
            int index = dtc.value("index", 0);
            // 扩展 vector
            while ((int)tool_calls.size() <= index) {
                tool_calls.emplace_back();
                tool_args_buf.emplace_back();
            }
            ToolCall& tc = tool_calls[index];
            if (dtc.contains("id") && !dtc["id"].is_null() && dtc["id"].is_string()) {
                tc.id = dtc["id"].get<std::string>();
            }
            if (dtc.contains("function")) {
                const auto& fn = dtc["function"];
                if (fn.contains("name") && !fn["name"].is_null() && fn["name"].is_string()) {
                    tc.name = fn["name"].get<std::string>();
                }
                if (fn.contains("arguments") && !fn["arguments"].is_null() && fn["arguments"].is_string()) {
                    std::string chunk = fn["arguments"].get<std::string>();
                    if (dtc.contains("id") && !dtc["id"].is_null()) {
                        // 首帧：覆盖
                        tool_args_buf[index] = chunk;
                    } else {
                        // 后续帧：拼接
                        tool_args_buf[index] += chunk;
                    }
                }
            }
        }
    }

    // 收尾：把累积的 arguments 字符串解析为 json 对象
    void finalizeToolCalls() {
        for (size_t i = 0; i < tool_calls.size(); i++) {
            if (i < tool_args_buf.size() && !tool_args_buf[i].empty()) {
                try {
                    tool_calls[i].arguments = json::parse(tool_args_buf[i]);
                } catch (...) {
                    tool_calls[i].arguments = json::object();
                }
            }
        }
    }
};

// SSE 行处理：解析 "data: {...}\n\n" 格式
static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    StreamContext* ctx = static_cast<StreamContext*>(userp);

    ctx->buffer.append(static_cast<char*>(contents), realsize);

    // 处理完整的行（以 \n 分割）
    size_t pos = 0;
    while (pos < ctx->buffer.size()) {
        size_t line_end = ctx->buffer.find('\n', pos);
        if (line_end == std::string::npos) {
            break;
        }

        std::string line = ctx->buffer.substr(pos, line_end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.rfind("data: ", 0) == 0) {
            std::string json_str = line.substr(6);

            if (json_str == "[DONE]") {
                ctx->continue_streaming = false;
            } else {
                try {
                    json chunk = json::parse(json_str);
                    if (chunk.contains("choices") && !chunk["choices"].empty()) {
                        const auto& choice = chunk["choices"][0];
                        if (choice.contains("delta")) {
                            const auto& delta = choice["delta"];

                            // 1. 累积 tool_calls 片段
                            ctx->accumulateToolCallChunk(delta);

                            // 2. 提取文本片段（content / reasoning_content）
                            std::string content;
                            if (delta.contains("content") && !delta["content"].is_null()
                                && delta["content"].is_string()) {
                                content = delta["content"].get<std::string>();
                            } else if (delta.contains("reasoning_content")
                                && !delta["reasoning_content"].is_null()
                                && delta["reasoning_content"].is_string()) {
                                content = delta["reasoning_content"].get<std::string>();
                            }
                            if (!content.empty()) {
                                ctx->full_content += content;
                                if (ctx->continue_streaming) {
                                    ctx->continue_streaming = ctx->callback(content);
                                }
                            }
                        }
                        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                            ctx->continue_streaming = false;
                        }
                    }
                } catch (const std::exception&) {
                    // 忽略解析错误，继续处理下一行
                }
            }
        }

        pos = line_end + 1;
    }

    // 移除已处理的数据
    if (pos > 0) {
        ctx->buffer.erase(0, pos);
    }

    return realsize;
}

LLMErrorType LLMClient::classifyError(long http_code, const std::string& message) {
    if (http_code == 401 || http_code == 403) {
        return LLMErrorType::AuthError;
    }
    if (http_code == 429) {
        return LLMErrorType::RateLimit;
    }
    if (http_code == 500 || http_code == 502 || http_code == 503) {
        return LLMErrorType::ServerError;
    }
    // context overflow: 400/422 包含上下文相关关键词
    if ((http_code == 400 || http_code == 422) &&
        (message.find("context") != std::string::npos ||
         message.find("too long") != std::string::npos ||
         message.find("too many tokens") != std::string::npos ||
         message.find("maximum context") != std::string::npos ||
         message.find("prompt too long") != std::string::npos ||
         message.find("token limit") != std::string::npos ||
         message.find("context_length") != std::string::npos ||
         message.find("Request size") != std::string::npos)) {
        return LLMErrorType::ContextOverflow;
    }
    if (http_code >= 400) {
        return LLMErrorType::ServerError;
    }
    return LLMErrorType::Unknown;
}

json LLMClient::sendRequest(const json& body) {
    CURL* curl = curl_easy_init();
    std::string response_string;

    if (!curl) {
        std::cerr << "[LLMClient] curl_easy_init failed" << std::endl;
        m_last_error = {LLMErrorType::NetworkError, "curl_easy_init failed", 0};
        return json{{"error", "curl init failed"}};
    }

    std::string full_url = m_base_url + "/v1/chat/completions";
    std::string json_str = body.dump();

    struct curl_slist* headers = nullptr;
      headers = curl_slist_append(headers, "Content-Type: application/json");
      if (!m_api_key.empty()) {
          std::string auth = "Authorization: Bearer "+ m_api_key;
          headers = curl_slist_append(headers, auth.c_str());
      }

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        LLMErrorType err_type = LLMErrorType::NetworkError;
        if (res == CURLE_OPERATION_TIMEDOUT) {
            err_type = LLMErrorType::Timeout;
        }
        std::cerr << "[LLMClient] curl error: " << curl_easy_strerror(res) << std::endl;
        m_last_error = {err_type, std::string("curl error: ") + curl_easy_strerror(res), 0};
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return json{{"error", m_last_error.message}};
    }

    if (http_code >= 400) {
        LLMErrorType err_type = classifyError(http_code, response_string);
        std::cerr << "[LLMClient] HTTP " << http_code << ": " << response_string << std::endl;
        m_last_error = {err_type, "HTTP " + std::to_string(http_code) + ": " + response_string, static_cast<int>(http_code)};
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return json{{"error", m_last_error.message}};
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    try {
        return json::parse(response_string);
    } catch (const std::exception& e) {
        std::cerr << "[LLMClient] JSON parse error: " << e.what() << std::endl;
        m_last_error = {LLMErrorType::ParseError, std::string("parse error: ") + e.what(), static_cast<int>(http_code)};
        return json{{"error", m_last_error.message}};
    }
}

// 重试配置：每个错误类型的重试次数和退避间隔(秒)
struct RetryConfig {
    int max_retries;
    int base_delay_sec;
};

static RetryConfig getRetryConfig(LLMErrorType type) {
    switch (type) {
        case LLMErrorType::NetworkError:
        case LLMErrorType::Timeout:
            return {3, 2};   // 3次, 2/4/8秒
        case LLMErrorType::RateLimit:
            return {3, 10}; // 3次, 10/20/40秒
        case LLMErrorType::ServerError:
            return {2, 5};  // 2次, 5/10秒
        default:
            return {0, 0};  // 不重试
    }
}

json LLMClient::sendRequestWithRetry(const json& body) {
    m_retry_count = 0;
    clearError();

    json last_response = sendRequest(body);
    if (!last_response.contains("error")) {
        return last_response;
    }

    LLMErrorType err_type = m_last_error.type;
    RetryConfig config = getRetryConfig(err_type);

    if (config.max_retries == 0) {
        std::cerr << "[LLMClient] Non-retryable error, giving up" << std::endl;
        return last_response;
    }

    std::cerr << "[LLMClient] Retrying... type=" << static_cast<int>(err_type)
              << " max_retries=" << config.max_retries << std::endl;

    for (int i = 0; i < config.max_retries; ++i) {
        m_retry_count = i + 1;
        int delay = config.base_delay_sec * (1 << i); // 指数退避
        std::cerr << "[LLMClient] Retry " << m_retry_count << "/" << config.max_retries
                  << " after " << delay << " seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delay));

        last_response = sendRequest(body);
        if (!last_response.contains("error")) {
            std::cerr << "[LLMClient] Retry succeeded after " << m_retry_count << " attempts" << std::endl;
            return last_response;
        }

        // 检查是否是可重试的错误
        if (m_last_error.type != err_type) {
            RetryConfig new_config = getRetryConfig(m_last_error.type);
            if (new_config.max_retries == 0) {
                std::cerr << "[LLMClient] New error is non-retryable, giving up" << std::endl;
                return last_response;
            }
        }
    }

    std::cerr << "[LLMClient] All retries exhausted after " << m_retry_count << " attempts" << std::endl;
    return last_response;
}

LLMResponse LLMClient::parseResponse(const json& response) const {
    LLMResponse resp;

    if (response.contains("error")) {
        resp.content = "Error: " + response["error"].get<std::string>();
        return resp;
    }

    try {
        const auto& message = response["choices"][0]["message"];
        resp.content = message.value("content", "");

        if (message.contains("tool_calls")) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall call;
                call.id = tc["id"].get<std::string>();
                call.name = tc["function"]["name"].get<std::string>();
                // arguments 是 JSON 字符串，解析为 json 对象
                std::string args_str = tc["function"]["arguments"].get<std::string>();
                call.arguments = json::parse(args_str);
                resp.tool_calls.push_back(call);
            }
            resp.has_tool_calls = !resp.tool_calls.empty();
        }
    } catch (const std::exception& e) {
        resp.content = std::string("Error parsing response: ") + e.what();
    }
    return resp;
}

std::vector<Message> LLMClient::buildMessages(
    const std::string& prompt,
    const std::string& /*history*/
) const {
    std::vector<Message> msgs;
    msgs.push_back(Message{
        "system",
        "You are a helpful AI assistant. When you need to use a tool, "
        "respond with a tool call in JSON format."
    });
    msgs.push_back(Message{"user", prompt});
    return msgs;
}

LLMResponse LLMClient::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools
) {
    json body;
    body["model"] = m_model_name;
    body["stream"] = false;

    // 构建 messages 数组
    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (msg.tool_call_id.has_value()) m["tool_call_id"] = *msg.tool_call_id;
        if (msg.name.has_value())         m["name"] = *msg.name;
        body["messages"].push_back(m);
    }

    // 添加工具定义（function calling schema）
    if (!tools.empty()) {
        for (const auto& tool : tools) {
            json t;
            t["type"] = "function";
            t["function"]["name"] = tool.name;
            t["function"]["description"] = tool.description;
            t["function"]["parameters"] = tool.parameters;
            body["tools"].push_back(t);
        }
        body["tool_choice"] = "auto";
    }

    json response = sendRequestWithRetry(body);
    return parseResponse(response);
}

// 兼容接口
std::string LLMClient::chat(const std::string& prompt, const std::string& history) {
    auto msgs = buildMessages(prompt, history);
    auto resp = chat(msgs, {});
    return resp.content;
}

LLMResponse LLMClient::generateStream(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    StreamCallback callback
) {
    LLMResponse resp;

    CURL* curl = curl_easy_init();
    if (!curl) {
        m_last_error = {LLMErrorType::NetworkError, "curl_easy_init failed", 0};
        return resp;
    }

    std::string full_url = m_base_url + "/v1/chat/completions";

    // 构建请求体
    json body;
    body["model"] = m_model_name;
    body["stream"] = true;

    for (const auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (msg.tool_call_id.has_value()) m["tool_call_id"] = *msg.tool_call_id;
        if (msg.name.has_value())         m["name"] = *msg.name;
        body["messages"].push_back(m);
    }

    if (!tools.empty()) {
        for (const auto& tool : tools) {
            json t;
            t["type"] = "function";
            t["function"]["name"] = tool.name;
            t["function"]["description"] = tool.description;
            t["function"]["parameters"] = tool.parameters;
            body["tools"].push_back(t);
        }
        body["tool_choice"] = "auto";
    }

    std::string json_str = body.dump();

    struct curl_slist* headers = nullptr;
     headers = curl_slist_append(headers, "Content-Type: application/json");
     headers = curl_slist_append(headers, "Accept: text/event-stream");
     if (!m_api_key.empty()) {
         std::string auth = "Authorization: Bearer " + m_api_key;
         headers = curl_slist_append(headers, auth.c_str());
     }

    StreamContext streamCtx(std::move(callback));

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &streamCtx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_stream_timeout); // 使用可配置的流式超时

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        LLMErrorType err_type = LLMErrorType::NetworkError;
        if (res == CURLE_OPERATION_TIMEDOUT) {
            err_type = LLMErrorType::Timeout;
        }
        m_last_error = {err_type, std::string("curl error: ") + curl_easy_strerror(res), 0};
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        resp.content = "Error: " + m_last_error.message;
        return resp;
    }

    if (http_code >= 400) {
        LLMErrorType err_type = classifyError(http_code, streamCtx.buffer);
        m_last_error = {err_type, "HTTP " + std::to_string(http_code) + ": " + streamCtx.buffer, static_cast<int>(http_code)};
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        resp.content = "Error: " + m_last_error.message;
        return resp;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 收尾：把累积的 tool_calls arguments 字符串解析为 json
    streamCtx.finalizeToolCalls();

    // 返回完整响应：content + tool_calls
    resp.content = streamCtx.full_content;
    resp.tool_calls = streamCtx.tool_calls;
    resp.has_tool_calls = !resp.tool_calls.empty();
    return resp;
}
