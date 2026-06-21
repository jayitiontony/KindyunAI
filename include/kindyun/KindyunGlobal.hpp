/**
 * @file KindyunGlobal.hpp
 * @brief 全局工具函数与类型定义 —— UTF-8/GBK 编码转换、全局配置
 *
 * 本头文件提供跨模块全局共享的功能：
 *   1. UTF-8 <-> GBK 编码转换（Windows 控制台编码适配）
 *   2. 全局输出字符集配置
 *   3. 便捷日志输出宏
 *
 */

#pragma once
#ifndef KINDYUN_GLOBAL_HPP
#define KINDYUN_GLOBAL_HPP
#include <string>
#include <vector>
#include <iostream>
#include <windows.h>
#include <csignal>
// ========== 编码转换 ==========

/**
 * convert_to_utf8 —— Windows ANSI 编码转 UTF-8。
 *
 * 在 Windows 上，std::cin 读取的文本使用系统默认 ANSI 编码（中文 Windows 下为 GBK/CP936），
 * 而 LLM API 和大多数工具输出使用 UTF-8。此函数用于将系统编码转换为 UTF-8。
 *
 * 实现原理：
 *   1. MultiByteToWideChar(CP_ACP, ...)：ANSI -> Unicode (UTF-16)
 *   2. WideCharToMultiByte(CP_UTF8, ...)：Unicode -> UTF-8
 *
 * @param str 输入的 ANSI 编码字符串
 * @return UTF-8 编码的字符串
 */
static std::string convert_to_utf8(const std::string& str) {
    if (str.empty()) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (wlen == 0) return "";
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), wbuf.data(), wlen);
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), (int)wbuf.size(), NULL, 0, NULL, NULL);
    std::vector<char> utf8buf(utf8len);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), (int)wbuf.size(), utf8buf.data(), utf8len, NULL, NULL);
    return std::string(utf8buf.data(), utf8len);
}

/**
 * convert_to_gbk —— UTF-8 编码转 Windows ANSI (GBK/CP936)。
 *
 * 在 Windows 中文环境下，std::cout 输出的文本使用系统 ANSI 编码。
 * LLM 返回的 JSON 内容使用 UTF-8。此函数用于将 UTF-8 文本转换为
 * 终端可正确显示的系统编码，避免乱码。
 *
 * @param str 输入的 UTF-8 编码字符串
 * @return GBK/ANSI 编码的字符串（可用于 std::cout 正确显示中文）
 */
static std::string convert_to_gbk(const std::string& str) {
    if (str.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (wlen == 0) return "";
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), wbuf.data(), wlen);
    int gbblen = WideCharToMultiByte(CP_ACP, 0, wbuf.data(), (int)wbuf.size(), NULL, 0, NULL, NULL);
    std::vector<char> gbbuf(gbblen);
    WideCharToMultiByte(CP_ACP, 0, wbuf.data(), (int)wbuf.size(), gbbuf.data(), gbblen, NULL, NULL);
    return std::string(gbbuf.data(), gbblen);
}

#endif // KINDYUN_GLOBAL_HPP
