#include "http_client.hpp"
#include <iostream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define closesocket closesocket
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define closesocket close
#define SOCKET int
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#endif

namespace sysmon {

HttpClient::HttpClient(int timeout_ms) : timeout_ms_(timeout_ms) {
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

HttpClient::~HttpClient() {
#ifdef _WIN32
    WSACleanup();
#endif
}

HttpResponse HttpClient::Get(const std::string& url) {
    return Request("GET", url, "");
}

HttpResponse HttpClient::Post(const std::string& url, const std::string& body) {
    return Request("POST", url, body);
}

HttpResponse HttpClient::Request(const std::string& method, const std::string& url, 
                                   const std::string& body) {
    HttpResponse response;
    response.success = false;
    response.status_code = 0;
    
    // Parse URL (simple parser for http://host:port/path)
    if (url.substr(0, 7) != "http://") {
        response.error = "Only HTTP URLs are supported";
        return response;
    }
    
    std::string host_port = url.substr(7);
    size_t path_pos = host_port.find('/');
    std::string path = "/";
    if (path_pos != std::string::npos) {
        path = host_port.substr(path_pos);
        host_port = host_port.substr(0, path_pos);
    }
    
    std::string host;
    int port = 80;
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        host = host_port.substr(0, colon_pos);
        try {
            port = std::stoi(host_port.substr(colon_pos + 1));
        } catch (...) {
            response.error = "Invalid port number";
            return response;
        }
    } else {
        host = host_port;
    }
    
    // Resolve hostname
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        response.error = "Failed to resolve host: " + host;
        return response;
    }
    
    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        response.error = "Failed to create socket";
        freeaddrinfo(result);
        return response;
    }
    
    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = timeout_ms_ / 1000;
    timeout.tv_usec = (timeout_ms_ % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Connect
    if (connect(sock, result->ai_addr, result->ai_addrlen) == SOCKET_ERROR) {
        response.error = "Failed to connect to " + host + ":" + std::to_string(port);
        closesocket(sock);
        freeaddrinfo(result);
        return response;
    }
    
    freeaddrinfo(result);
    
    // Build HTTP request
    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/json\r\n";
        request << "Content-Length: " << body.size() << "\r\n";
    }
    request << "Connection: close\r\n";
    request << "\r\n";
    if (!body.empty()) {
        request << body;
    }
    
    std::string request_str = request.str();
    
    // Send request
    ssize_t sent = send(sock, request_str.c_str(), request_str.size(), 0);
    if (sent != static_cast<ssize_t>(request_str.size())) {
        response.error = "Failed to send HTTP request";
        closesocket(sock);
        return response;
    }
    
    // Read response
    std::ostringstream response_stream;
    char buffer[4096];
    ssize_t received;
    
    while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[received] = '\0';
        response_stream << buffer;
    }
    
    closesocket(sock);
    
    if (received < 0) {
        response.error = "Failed to receive HTTP response";
        return response;
    }
    
    std::string full_response = response_stream.str();
    
    // Parse response
    size_t header_end = full_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        response.error = "Invalid HTTP response";
        return response;
    }
    
    std::string headers = full_response.substr(0, header_end);
    response.body = full_response.substr(header_end + 4);
    
    // Parse status code
    size_t status_pos = headers.find("HTTP/");
    if (status_pos != std::string::npos) {
        size_t code_start = headers.find(' ', status_pos);
        size_t code_end = headers.find(' ', code_start + 1);
        if (code_start != std::string::npos && code_end != std::string::npos) {
            try {
                response.status_code = std::stoi(headers.substr(code_start + 1, code_end - code_start - 1));
                response.success = (response.status_code >= 200 && response.status_code < 300);
            } catch (...) {
                response.error = "Failed to parse status code";
            }
        }
    }
    
    if (!response.success && response.status_code == 0) {
        response.error = "Invalid status code";
    }
    
    return response;
}

} // namespace sysmon
