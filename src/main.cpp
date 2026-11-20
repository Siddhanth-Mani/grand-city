#include "core/logger.h"
#include "engine/application.h"
int main(int argc, char** argv) {
    try {
        gc6::Application app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        gc6::Logger::error(std::string("Fatal: ") + e.what());
        return 1;
    }
}
