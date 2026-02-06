#pragma once

#include <string>

namespace sysmon {

struct HttpResponse {
    bool success;
    int status_code;
    std::string body;
    std::string error;
};

class HttpClient {
public:
    explicit HttpClient(int timeout_ms = 5000);
    ~HttpClient();
    
    HttpResponse Get(const std::string& url);
    HttpResponse Post(const std::string& url, const std::string& body);
    
private:
    int timeout_ms_;
    
    HttpResponse Request(const std::string& method, const std::string& url, 
                         const std::string& body);
};

} // namespace sysmon
