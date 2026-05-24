#pragma once

#include <vector>
#include <string>

std::vector<int> internetTest(const std::vector<std::string>& hosts);
int ping(const std::string& host);
int ping(const std::string& host, int port = 443, int timeoutMs = 1000);