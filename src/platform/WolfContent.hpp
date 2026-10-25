// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>
namespace wolf_content {
struct WolfContent { std::string directory,edition,title,saveKey,reason; bool supported=false; };
bool inspectWolf(const std::string &directory,WolfContent &content);
}
