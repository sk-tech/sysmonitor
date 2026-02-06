#include "sysmon/network_publisher.hpp"
#include <iostream>
#include <sstream>
#include <ctime>
#include <cstring>
#include <algorithm>

// Simple HTTP client without external dependencies
// For production, consider using libcurl
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace sysmon {

NetworkPublisher::NetworkPublisher(const AgentConfig& config)
    : config_(config) {
    last_publish_time_ = std::chrono::steady_clock::now();
}

NetworkPublisher::~NetworkPublisher() {
    Stop();
}

void NetworkPublisher::Start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    publish_thread_ = std::thread(&NetworkPublisher::PublishLoop, this);
    
    std::cout << "Network publisher started" << std::endl;
    std::cout << "  Aggregator: " << config_.aggregator_url << std::endl;
    std::cout << "  Interval: " << config_.push_interval_ms << "ms" << std::endl;
    std::cout << "  Queue size: " << config_.max_queue_size << std::endl;
}

void NetworkPublisher::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (publish_thread_.joinable()) {
        publish_thread_.join();
    }
    
    std::cout << "Network publisher stopped" << std::endl;
}

bool NetworkPublisher::QueueMetric(const PublishableMetric& metric) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (metric_queue_.size() >= static_cast<size_t>(config_.max_queue_size)) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.queue_overflows++;
        return false;
    }
    
    metric_queue_.push(metric);
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.metrics_queued++;
    
    return true;
}

bool NetworkPublisher::QueueCPUMetrics(const CPUMetrics& metrics) {
    int64_t timestamp = std::time(nullptr);
    
    bool success = true;
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.total_usage", metrics.total_usage));
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.num_cores", metrics.num_cores));
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.load_average_1m", metrics.load_average_1m));
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.load_average_5m", metrics.load_average_5m));
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.load_average_15m", metrics.load_average_15m));
    success &= QueueMetric(PublishableMetric(timestamp, "cpu.context_switches", metrics.context_switches));
    
    return success;
}

bool NetworkPublisher::QueueMemoryMetrics(const MemoryMetrics& metrics) {
    int64_t timestamp = std::time(nullptr);
    
    bool success = true;
    success &= QueueMetric(PublishableMetric(timestamp, "memory.total_bytes", metrics.total_bytes));
    success &= QueueMetric(PublishableMetric(timestamp, "memory.used_bytes", metrics.used_bytes));
    success &= QueueMetric(PublishableMetric(timestamp, "memory.free_bytes", metrics.free_bytes));
    success &= QueueMetric(PublishableMetric(timestamp, "memory.available_bytes", metrics.available_bytes));
    success &= QueueMetric(PublishableMetric(timestamp, "memory.usage_percent", metrics.usage_percent));
    
    return success;
}

size_t NetworkPublisher::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return metric_queue_.size();
}

NetworkPublisher::Stats NetworkPublisher::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void NetworkPublisher::PublishLoop() {
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_publish_time_
        ).count();
        
        // Check if it's time to publish
        if (elapsed >= config_.push_interval_ms) {
            // Collect batch of metrics
            std::vector<PublishableMetric> batch;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                
                // Take up to 100 metrics per batch
                int batch_size = std::min(100, static_cast<int>(metric_queue_.size()));
                batch.reserve(batch_size);
                
                for (int i = 0; i < batch_size; ++i) {
                    if (!metric_queue_.empty()) {
                        batch.push_back(metric_queue_.front());
                        metric_queue_.pop();
                    }
                }
            }
            
            // Publish batch with retry logic
            if (!batch.empty()) {
                bool success = false;
                
                for (int attempt = 0; attempt < config_.retry_max_attempts; ++attempt) {
                    if (PublishBatch(batch)) {
                        success = true;
                        break;
                    }
                    
                    // Exponential backoff
                    if (attempt < config_.retry_max_attempts - 1) {
                        int delay = CalculateBackoffDelay(attempt);
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                    }
                }
                
                if (!success) {
                    std::cerr << "Failed to publish batch after " 
                              << config_.retry_max_attempts << " attempts" << std::endl;
                    
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.metrics_failed += batch.size();
                }
            }
            
            last_publish_time_ = now;
        }
        
        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool NetworkPublisher::PublishBatch(const std::vector<PublishableMetric>& batch) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.publish_attempts++;
    
    std::string json = BuildJsonPayload(batch);
    
    bool success = SendHTTPRequest(json);
    
    if (success) {
        stats_.publish_successes++;
        stats_.metrics_sent += batch.size();
    } else {
        stats_.publish_failures++;
    }
    
    return success;
}

std::string NetworkPublisher::BuildJsonPayload(const std::vector<PublishableMetric>& metrics) {
    std::ostringstream json;
    
    json << "{";
    json << "\"hostname\":\"" << config_.hostname << "\",";
    json << "\"version\":\"0.5.0\",";
    json << "\"platform\":\"";
    
#if defined(__linux__)
    json << "Linux";
#elif defined(_WIN32)
    json << "Windows";
#elif defined(__APPLE__)
    json << "macOS";
#else
    json << "Unknown";
#endif
    
    json << "\",";
    
    // Add host tags
    if (!config_.host_tags.empty()) {
        json << "\"tags\":{";
        bool first_tag = true;
        for (const auto& tag : config_.host_tags) {
            if (!first_tag) json << ",";
            json << "\"" << tag.first << "\":\"" << tag.second << "\"";
            first_tag = false;
        }
        json << "},";
    } else {
        json << "\"tags\":{},";
    }
    
    // Add metrics array
    json << "\"metrics\":[";
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"timestamp\":" << metrics[i].timestamp << ",";
        json << "\"metric_type\":\"" << metrics[i].metric_type << "\",";
        json << "\"value\":" << metrics[i].value;
        if (!metrics[i].tags.empty()) {
            json << ",\"tags\":\"" << metrics[i].tags << "\"";
        }
        json << "}";
    }
    json << "]";
    json << "}";
    
    return json.str();
}

bool NetworkPublisher::SendHTTPRequest(const std::string& json_body) {
    // Parse URL (support http:// and https://)
    std::string url = config_.aggregator_url;
    bool use_tls = false;
    std::string host_port;
    
    if (url.substr(0, 8) == "https://") {
        use_tls = true;
        host_port = url.substr(8);
    } else if (url.substr(0, 7) == "http://") {
        use_tls = false;
        host_port = url.substr(7);
    } else {
        std::cerr << "Invalid URL scheme (expected http:// or https://)" << std::endl;
        return false;
    }
    
    if (use_tls) {
        std::cerr << "HTTPS not yet fully implemented - use HTTP or compile with libcurl" << std::endl;
        // For now, fall back to HTTP
        // In production, would use OpenSSL or libcurl for TLS
        std::cerr << "Attempting HTTP fallback..." << std::endl;
        use_tls = false;
    }
    
    size_t path_pos = host_port.find('/');
    std::string path = "/api/metrics";
    if (path_pos != std::string::npos) {
        path = host_port.substr(path_pos);
        host_port = host_port.substr(0, path_pos);
    }
    
    std::string host;
    int port = use_tls ? 9443 : 9000;
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        host = host_port.substr(0, colon_pos);
        port = std::stoi(host_port.substr(colon_pos + 1));
    } else {
        host = host_port;
    }
    
    // Resolve hostname
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve host: " << host << std::endl;
        return false;
    }
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        freeaddrinfo(result);
        return false;
    }
    
    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = config_.http_timeout_ms / 1000;
    timeout.tv_usec = (config_.http_timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Connect
    if (connect(sock, result->ai_addr, result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
        closesocket(sock);
        freeaddrinfo(result);
        return false;
    }
    
    freeaddrinfo(result);
    
    // Build HTTP request
    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << json_body.size() << "\r\n";
    request << "X-SysMon-Token: " << config_.auth_token << "\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";
    request << json_body;
    
    std::string request_str = request.str();
    
    // Send request
    ssize_t sent = send(sock, request_str.c_str(), request_str.size(), 0);
    if (sent != static_cast<ssize_t>(request_str.size())) {
        std::cerr << "Failed to send HTTP request" << std::endl;
        closesocket(sock);
        return false;
    }
    
    // Read response (just check status code)
    char buffer[1024];
    ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    closesocket(sock);
    
    if (received <= 0) {
        std::cerr << "Failed to receive HTTP response" << std::endl;
        return false;
    }
    
    buffer[received] = '\0';
    std::string response(buffer);
    
    // Check for 200 OK
    if (response.find("200 OK") != std::string::npos) {
        return true;
    }
    
    std::cerr << "HTTP request failed: " << response.substr(0, 100) << std::endl;
    return false;
}

int NetworkPublisher::CalculateBackoffDelay(int attempt) {
    // Exponential backoff: base_delay * 2^attempt
    int delay = config_.retry_base_delay_ms * (1 << attempt);
    return std::min(delay, 30000); // Cap at 30 seconds
}

} // namespace sysmon
