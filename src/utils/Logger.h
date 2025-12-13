#pragma once
#include <cstring>
#include <cstdio>

// 提取文件名（不带路径）
inline const char* getBaseName(const char* filePath) {
    const char* baseName = std::strrchr(filePath, '/'); // Unix 风格路径
    if (!baseName) baseName = std::strrchr(filePath, '\\'); // Windows 风格路径
    return baseName ? baseName + 1 : filePath; // 返回文件名部分或原始路径
}

// 日志等级（数字越大，等级越高）
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_NONE  4

// 当前日志等级：发布版改成 LOG_LEVEL_WARN 即可关闭调试输出
#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// 实现接口（带文件名和行号）
// std::printf("[%s] %s:%d | " fmt "\n", tag, __FILE__, __LINE__, ##__VA_ARGS__); 这是带完整的路径
#define LOG_IMPL(level, tag, fmt, ...)                                      \
    do {                                                                    \
        if (CURRENT_LOG_LEVEL <= level) {                                   \
            std::printf("[%s] %s:%d | " fmt "\n", tag, getBaseName(__FILE__), __LINE__, ##__VA_ARGS__);\
        }                                                                   \
    } while (0)

// 外部调用的宏
#define LOG_DEBUG(fmt, ...) LOG_IMPL(LOG_LEVEL_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_IMPL(LOG_LEVEL_INFO,  "INFO",  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_IMPL(LOG_LEVEL_WARN,  "WARN",  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_IMPL(LOG_LEVEL_ERROR, "ERROR", fmt, ##__VA_ARGS__)
