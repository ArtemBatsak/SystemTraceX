#pragma once

#include <string>
#include <sstream>
#include "httplib.h"
#include "web_helper.h" // твой Snapshot + helper

#include <fstream>
#include <memory>
#include <mutex>

class Web
{
public:
    explicit Web(WebTelemetryHelper& webHelper_);

    void Start(const std::string& host = "0.0.0.0", int port = 8080);
    void Stop();


private:
    WebTelemetryHelper& webHelper;

    std::unique_ptr<httplib::Server> svr;
};



static std::string ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
        return "";

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}