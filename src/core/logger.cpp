#include "core/logger.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
namespace gc6 {
namespace {
std::ofstream g_file;
std::mutex g_mutex;
std::string g_lastInfo;
unsigned g_infoCount = 0;
std::string stamp() {
    auto t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}
void write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string line = "[" + stamp() + "] [" + level + "] " + msg;
    std::cout << line << std::endl;
    if (g_file.is_open()) g_file << line << std::endl;
}
} // namespace
void Logger::init(const std::string& filePath) {
    g_file.open(filePath, std::ios::app);
    info("Logger initialised.");
}
void Logger::info(const std::string& msg) {
    write("INFO", msg);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lastInfo = msg;
    ++g_infoCount;
}
std::string Logger::lastInfo() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lastInfo;
}
unsigned Logger::infoCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_infoCount;
}
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }
void Logger::shutdown() {
    info("Logger shutting down.");
    if (g_file.is_open()) g_file.close();
}
} // namespace gc6
