#include <cstdio>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "../../include/rtmp_server.hpp"

using namespace rtmp;

// Callbacks
void onConnect(std::shared_ptr<RTMPSession> session) {
    std::cout << "Client connected: " << session->getStreamInfo().client_ip << std::endl;
}

void onPublish(std::shared_ptr<RTMPSession> session, const std::string& app, const std::string& key) {
    std::cout << "Publish from " << session->getStreamInfo().client_ip << ": " << app << "/" << key << std::endl;
}

bool onLog(const std::string& message, const rtmp::LogLevel& level) {
    std::string levelStr;
    switch (level) {
        case LogLevel::ERROR: levelStr = "ERROR"; break;
        case LogLevel::WARN: levelStr = "WARN"; break;
        case LogLevel::INFO: levelStr = "INFO"; break;
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
    }
    std::cout << "[" << levelStr << "] " << message << std::endl;
    return true;
}

static struct termios g_orig_termios;

static void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

static bool setup_nonblocking_stdin() {
    struct termios raw;
    int flags;

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0)
        return false;

    raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return false;

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0)
        return false;

    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
        return false;

    std::atexit(restore_terminal);
    return true;
}

int main() {
    // Set log level
    Logger::getInstance().setLevel(LogLevel::DEBUG);

    // Setup non-blocking stdin
    if (!setup_nonblocking_stdin()) {
        std::cerr << "Failed to configure terminal input" << std::endl;
        return 1;
    }

    // Create RTMP server on port 1935
    RTMPServer server(1935);

    // Set callbacks
    server.setOnConnect(onConnect);
    server.setOnPublish(onPublish);
    Logger::getInstance().setOnLog(onLog);

    // Enable GOP cache
    server.enableGOPCache(true);

    bool isRunning = false;
    if (!server.start(isRunning)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "RTMP server running. Press 'q' to stop." << std::endl;

    while (isRunning) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n == 1 && (ch == 'q' || ch == 'Q')) {
                std::cout << "Shutting down..." << std::endl;
                server.stop();
                break;
            }
        }
    }

    return 0;
}
