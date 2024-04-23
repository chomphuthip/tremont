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
    int addrInfoResult = getaddrinfo("127.0.0.1", "7777", &hints, &result);
    if (addrInfoResult != 0) {
        std::cerr << "bad addr" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    //init socket
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        std::cerr << "bad socket" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    //bind the socket
    int bindResult = bind(sock, result->ai_addr, (int)result->ai_addrlen);
    if (bindResult == SOCKET_ERROR) {
        std::cerr << "bad bind" << std::endl;
        closesocket(sock);
        freeaddrinfo(result);
        WSACleanup();
        return 1;

    }

    //init nexus
    Tremont_Nexus* nexus = 0;
    int res = 0;
    
    res = tremont_init_nexus(&nexus);
    if (res != 0) {
        std::cerr << "couldn't init nexus" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }


    //bind nexus to port
    res = tremont_bind_nexus(sock, nexus);
    if (res != 0) {
        std::cerr << "couldn't bind nexus" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    //get remote addrinfo
    struct addrinfo remote_hint, *remote_info;
    memset(&remote_hint, 0, sizeof(remote_hint));
    remote_hint.ai_family = AF_INET;
    remote_hint.ai_socktype = SOCK_DGRAM;
    remote_hint.ai_protocol = IPPROTO_UDP;
    addrInfoResult = getaddrinfo("127.0.0.1", "9999", &remote_hint, &remote_info);
    if (addrInfoResult != 0) {
        std::cerr << "bad remote addr" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }


    std::cout << "requesting a stream" << std::endl;
    //request the stream
    res = tremont_req_stream(9999, remote_info->ai_addr, 5, nexus);
    if (res == -1) {
        std::cerr << "req timeout" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    std::cout << "got a stream" << std::endl;

    byte msg[16] = "Sending tone...";

    res = tremont_send(9999, msg, 16, nexus);
    if (res == -1) {
        std::cerr << "ack timeout" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    byte temp[99];
    ZeroMemory(temp, 99);
    res = tremont_recv(9999, temp, 99, nexus);
    byte* data = (byte*)malloc(res);
    if (data == 0) {
        std::cerr << "unable to malloc memory for data" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }
    memcpy(data, temp, res);
    std::cout << data << std::endl;
    free(data);

    tremont_end_stream(9999, nexus);
    tremont_destroy_nexus(nexus);

    freeaddrinfo(result);
    WSACleanup();

    return 0;
}
