#pragma once
#include <string>
namespace gc6 {
class Logger {
public:
    static void init(const std::string& filePath);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void shutdown();
    // Latest INFO line and how many INFO lines have been written (the on-screen
    // overlay shows new ones as a short toast).
    static std::string lastInfo();
    static unsigned infoCount();
};
} // namespace gc6
