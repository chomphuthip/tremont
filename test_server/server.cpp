#include <iostream>

#include<winsock2.h>
#include<ws2tcpip.h>

#include "tremont.h"


int main() {
    //init winsock
    WSADATA winSocketData;
    int startupResult;
    startupResult = WSAStartup(MAKEWORD(2, 2), &winSocketData);
    if (startupResult != 0) {
        std::cerr << "startup error" << std::endl;
        WSACleanup();
        return 1;
    }

    //get addr info
    struct addrinfo* result = NULL, hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    int addrInfoResult = getaddrinfo("127.0.0.1", "9999", &hints, &result);
    if (addrInfoResult != 0) {
        std::cerr << "bad addr" << std::endl;
        std::cin.get();
        return 1;
    }

    //init socket
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        std::cerr << "bad socket" << std::endl;
        std::cin.get();
        return 1;
    }
    
    //bind the socket
    int bindResult = bind(sock, result->ai_addr, (int)result->ai_addrlen);
    if (bindResult == SOCKET_ERROR) {
        std::cerr << "bad bind" << std::endl;
        std::cin.get();
        return 1;

    }

    //init nexus
    Tremont_Nexus* nexus = 0;
    int res = 0;

    res = tremont_init_nexus(&nexus);
    if (res != 0) {
        std::cerr << "couldn't init nexus" << std::endl;
        std::cin.get();
        return 1;
    }

    //bind nexus to the socket
    res = tremont_bind_nexus(sock, nexus);
    if (res != 0) {
        std::cerr << "couldn't bind nexus" << std::endl;
        std::cin.get();
        return 1;
    }
    std::cout << "Nexus bound!" << std::endl;

    //accept a stream request
    res = 1;
    /*
    while (res != 0) {
        std::cout << "Accepting a stream..." << std::endl;
        res = tremont_accept_stream(9999, 5, nexus);
    }
    */
    tremont_accept_stream(9999, 0, nexus);
    std::cout << "Connected!" << std::endl;

    byte temp[99];
    ZeroMemory(temp, 99);
    res = tremont_recv(9999, temp, 16, nexus);
    byte* data = (byte*)malloc(res);
    if (data == 0) {
        std::cerr << "unable to malloc memory for data" << std::endl;
        std::cin.get();
        return 1;
    }
    std::cout << "Recieved data:" << std::endl;
    memcpy(data, temp, res);
    std::cout << data << std::endl;
    free(data);

    byte msg[32] = "i really like car seat headrest";

    res = tremont_send(9999, msg, 32, nexus);
    if (res == -1) {
        std::cerr << "ack timeout" << std::endl;
        std::cin.get();
        return 1;
    }

    tremont_end_stream(9999, nexus);
    tremont_destroy_nexus(nexus);

    freeaddrinfo(result);
    closesocket(sock);
    WSACleanup();

    std::cin.get();
    return 0;
}