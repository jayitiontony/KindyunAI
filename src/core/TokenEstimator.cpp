/**
 * @file TokenEstimator.cpp
 * @brief Token 估算器实现
 */

#include "kindyun/TokenEstimator.hpp"

size_t TokenEstimator::estimate(const std::string& text) {
    if (text.empty()) {
        return 1; // 空文本至少算 1 token
    }

    size_t tokens = 0;
    size_t i = 0;
    const size_t len = text.size();

    while (i < len) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (c < 128) {
            // ASCII 字符：4 字符 ≈ 1 token
            // 统计连续 ASCII 字符数量
            size_t asciiCount = 0;
            while (i < len && static_cast<unsigned char>(text[i]) < 128) {
                asciiCount++;
                i++;
            }
            // 每 4 个 ASCII 字符算 1 token
            tokens += (asciiCount + 3) / 4;
        } else if ((c & 0xE0) == 0xC0) {
            // UTF-8 两字节字符（拉丁扩展等）
            //约 2 字符 ≈ 1 token
            tokens += 1;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // UTF-8 三字节字符（中日韩文字等）
            // 约 2 字符 ≈ 1 token
            tokens += 1;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // UTF-8 四字节字符（表情符号等）
            tokens += 2;
            i += 4;
        } else {
            // 其他情况，跳过一字节
            tokens += 1;
            i++;
        }
    }

    // 确保至少返回 1
    return tokens > 0 ? tokens : 1;
}

size_t TokenEstimator::estimateMessage(const Message& msg) {
    // 内容估算
    size_t contentTokens = estimate(msg.content);

    // Role overhead：约 4 tokens（"role": "xxx", "\n" 等）
    size_t overhead = 4;

    // tool 角色额外 overhead
    if (msg.role == "tool") {
        overhead += 4;
        if (msg.name.has_value()) {
            overhead += estimate(msg.name.value());
        }
    }

    return contentTokens + overhead;
}

size_t TokenEstimator::estimateTotal(const std::vector<Message>& msgs) {
    size_t total = 0;
    for (const auto& msg : msgs) {
        total += estimateMessage(msg);
    }
    return total;
}

size_t TokenEstimator::estimateWithSystem(const std::string& systemPrompt,
                                        const std::vector<Message>& msgs) {
    size_t total = 0;

    // System prompt
    if (!systemPrompt.empty()) {
        // System overhead约 4 tokens
        total += estimate(systemPrompt) + 4;
    }

    // Messages
    total += estimateTotal(msgs);

    return total;
}