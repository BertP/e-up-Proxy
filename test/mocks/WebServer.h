#ifndef MOCK_WEBSERVER_H
#define MOCK_WEBSERVER_H

#include <string>
#include <cstddef>

#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

class WebServer {
public:
    WebServer(int port) {}
    void setContentLength(size_t len) {}
    void send(int status_code, const char* content_type, const char* body) {}
    void sendContent(const char* buf, size_t size) {}
    void sendContent(const char* buf) {}
    void sendContent(const std::string& str) {}
};

#endif // MOCK_WEBSERVER_H
