#include "funk.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#pragma comment(lib, "ws2_32.lib")

std::vector<int> internetTest(const std::vector<std::string>& hosts) {
    std::vector<int> results;
    for (const auto& host : hosts) {
        int latency = ping(host, 443, 1000);
        results.push_back(latency);
    }
    return results;
}

int ping(const std::string & host, int port, int timeoutMs)
    {
        static bool init = false;
        if (!init)
        {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                return -1;
            init = true;
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
            return -1;

        int ping = -1;

        for (auto p = res; p; p = p->ai_next)
        {
            SOCKET sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET)
                continue;

            auto start = std::chrono::high_resolution_clock::now();

            int result = connect(sock, p->ai_addr, (int)p->ai_addrlen);

            auto end = std::chrono::high_resolution_clock::now();

            if (result == 0)
            {
                ping = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                closesocket(sock);
                break;
            }

            closesocket(sock);
        }

        freeaddrinfo(res);
        return ping;
    }
#endif

#ifdef __linux__
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <chrono>
#include <string>

std::vector<int> internetTest(const std::vector<std::string>&hosts) {
        std::vector<int> results;
        for (const auto& host : hosts) {
            int latency = ping(host);
            results.push_back(latency);
        }
        return results;
    }

int ping(const std::string& host, int port, int timeoutMs)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
            return -1;

        int ping = -1;

        for (auto p = res; p; p = p->ai_next)
        {
            int sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock < 0)
                continue;

            auto start = std::chrono::high_resolution_clock::now();

            int result = connect(sock, p->ai_addr, p->ai_addrlen);

            auto end = std::chrono::high_resolution_clock::now();

            if (result == 0)
            {
                ping = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                close(sock);
                break;
            }

            close(sock);
        }

        freeaddrinfo(res);
        return ping;
    }
#endif 