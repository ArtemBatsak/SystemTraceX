#pragma once

#include <vector>
#include <string>

std::vector<int> internetTest(const std::vector<std::string>& hosts);
int ping_windows(const std::string& host);
int ping_linux(const std::string& host);