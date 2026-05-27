#pragma once

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "httplib.h"
#include "web_helper.h"
#include "../func/funk.h"

class Web {
public:
    explicit Web(WebTelemetryHelper& webHelper_);

    void Start(const std::string& host = "0.0.0.0", int port = 8080);
    void Stop();

private:
    WebTelemetryHelper& webHelper;
   // TaskLogger* taskLogger_;
    std::unique_ptr<httplib::Server> svr;
};

static std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
