// SPDX-License-Identifier: GPL-2.0-only
#include "WolfContent.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <map>
#include <algorithm>
#include <cstring>
namespace wolf_content {
bool inspectWolf(const std::string &directory,WolfContent &out) {
    namespace fs=std::filesystem;
    out={}; out.directory=directory; out.title="Wolfenstein 3D";
    std::error_code ec; std::map<std::string,fs::path> files;
    for(const auto &e:fs::directory_iterator(directory,ec)) {
        if(!e.is_regular_file(ec))continue;
        std::string name=e.path().filename().string(); std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);}); files[name]=e.path();
    }
    std::string ext; for(const char *x:{"wl6","wl3","wl1"}) if(files.count(std::string("vswap.")+x)) { ext=x; break; }
    if(ext.empty())return false;
    CC_SHA256_CTX hash; CC_SHA256_Init(&hash);
    std::string identity="metalwolf-v1:"+ext; CC_SHA256_Update(&hash,identity.data(),CC_LONG(identity.size()));
    for(const char *stem:{"vswap","gamemaps","maphead","vgadict","vgagraph","vgahead","audiot","audiohed"}) {
        auto found=files.find(std::string(stem)+"."+ext);
        if(found==files.end()) { out.reason="The selected folder is missing part of the Wolfenstein game data."; return true; }
        auto size=fs::file_size(found->second,ec); if(ec || !size || size>64*1024*1024) { out.reason="The game data contains an invalid or oversized file.";return true; }
        if(std::string(stem)=="vgadict" && size!=1024) { out.reason="This Wolfenstein dictionary format is unsupported.";return true; }
        if(std::string(stem)=="vgahead") {
            // The graphics table is compiled into the original executable; pick
            // the corresponding v1.4 edition instead of bypassing its checks.
            if(ext=="wl1" && size==471)out.edition="shareware";
            else if(ext=="wl6" && size==450)out.edition="gt";
            else if((ext=="wl6" && size==486) || (ext=="wl3" && size==477))out.edition="apogee";
            else { out.reason="This Wolfenstein revision needs a different original executable layout. Supported: PC v1.4 shareware, Apogee and GT/Activision.";return true; }
        }
        // Length-prefix each file to prevent ambiguous concatenated identities.
        CC_SHA256_Update(&hash,stem,CC_LONG(strlen(stem)+1)); uint64_t length=size; CC_SHA256_Update(&hash,&length,sizeof(length));
        std::ifstream file(found->second,std::ios::binary); std::array<char,65536> chunk;
        while(file) { file.read(chunk.data(),chunk.size()); CC_SHA256_Update(&hash,chunk.data(),CC_LONG(file.gcount())); }
        if(!file.eof()) { out.reason="Could not read the complete game data.";return true; }
    }
    unsigned char digest[32]; CC_SHA256_Final(digest,&hash); static const char hex[]="0123456789abcdef";
    for(unsigned char c:digest) { out.saveKey+=hex[c>>4];out.saveKey+=hex[c&15]; }
    out.title=ext=="wl1"?"Wolfenstein 3D: Escape from Wolfenstein":ext=="wl3"?"Wolfenstein 3D: Original Trilogy":"Wolfenstein 3D";
    out.supported=true; return true;
}
}
