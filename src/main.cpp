#include "fastdb/server.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>

static fastdb::Server* g_server = nullptr;

static void signal_handler(int sig) {
    std::cout << "\n[FastDB] Received signal " << sig << ", shutting down gracefully...\n";
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 5000;
    const char* env_port = std::getenv("PORT");
    if (env_port && *env_port) {
        try {
            port = std::stoi(env_port);
        } catch (...) {}
    }

    std::string publicDir = "";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-d" || arg == "--public") && i + 1 < argc) {
            publicDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: fastdb_server [options]\n"
                      << "Options:\n"
                      << "  -p, --port <port>       Port to listen on (default: 5000 or $PORT)\n"
                      << "  -d, --public <dir>      Public directory for web dashboard\n"
                      << "  -h, --help              Show this help message\n";
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================\n"
              << "       FastDB C++ Real-Time Server      \n"
              << "========================================\n";

    fastdb::Server server(port, publicDir);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "[FastDB] Fatal: Could not start server on port " << port << std::endl;
        return 1;
    }

    std::cout << "[FastDB] Web Dashboard and Real-Time API live on port " << port << std::endl;
    server.run();

    std::cout << "[FastDB] Server stopped cleanly. Goodbye!\n";
    return 0;
}
