#pragma once
#include <istream>
#include <string>
bool desktopUiCommand(const std::string &command, std::istream &input, const std::string &directory, std::string &error);
