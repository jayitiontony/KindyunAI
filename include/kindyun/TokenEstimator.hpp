/**
 * @file TokenEstimator.hpp
 * @brief Token 估算工具
 *
 * 提供简单的 token 数量估算功能，用于上下文压缩决策。
 * 采用字符统计近似算法：
 *   - 中文：约 2 个字符 ≈ 1 token
 *   - 英文：约 4 个字符 ≈ 1 token
 *
 * Phase 3 实现（Phase 4 可考虑接入 GPT-2 BPE 词表做精确估算）
 */

#pragma once
#include <string>
#include <vector>
#include "kindyun/Types.hpp"

/**
 * @brief Token 估算器
 *
 * 提供多种粒度的 token 估算方法，支持：
 *   - 单段文本估算
 *   - 单条消息估算
 *   -完整对话历史估算
 */
class TokenEstimator {
public:
    /**
     * @brief 估算一段文本的 token 数
     *
     * 使用简单字符统计算法：
     *   - 遍历字符串，按字节统计
     *   - 中文字符（UTF-8 多字节）在 3-4 字节范围内
     *   - 英文/数字等 ASCII 字符按 0.25 token/char 计算
     *   - 中文字符按 0.5 token/char 计算
     *
     * @param text 输入文本
     * @return 估算的 token 数量（始终 >= 1）
     */
    static size_t estimate(const std::string& text);

    /**
     * @brief 估算单条消息的 token 数
     *
     * 消息 token 数 = 内容 token 数 + role 标签 overhead（约 4 tokens）
     *
     * @param msg 消息结构
     * @return 估算的 token 数量
     */
    static size_t estimateMessage(const Message& msg);

    /**
     * @brief 估算一组消息的总 token 数
     *
     * @param msgs 消息列表
     * @return估算的总 token 数
     */
    static size_t estimateTotal(const std::vector<Message>& msgs);

    /**
     * @brief 估算带 system prompt 的完整消息列表
     *
     * @param systemPrompt system prompt 文本（可为空）
     * @param msgs 消息列表
     * @return估算的总 token 数
     */
    static size_t estimateWithSystem(const std::string& systemPrompt,
                                    const std::vector<Message>& msgs);
};