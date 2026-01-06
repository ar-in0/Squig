#include <iostream>
#include "squig/rtmp_server.h"

int main() {
    std::cout <<"Sup bros" << std::endl;

    // start server
    TCPServer tcp_server(1935);
}