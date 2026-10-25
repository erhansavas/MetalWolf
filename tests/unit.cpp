// SPDX-License-Identifier: GPL-2.0-only
// Game data detection with format-only fixtures (no game content).
#include "WolfContent.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs=std::filesystem;
static void fixture(const std::string &folder,const char *ext,int vgahead,bool upper) {
    fs::remove_all(folder); fs::create_directories(folder);
    for(const char *stem:{"vswap","gamemaps","maphead","vgadict","vgagraph","vgahead","audiot","audiohed"}) {
        std::string name=std::string(stem)+"."+ext;
        if(upper) for(char &c:name) c=char(toupper(c));
        size_t size=std::string(stem)=="vgadict"?1024:std::string(stem)=="vgahead"?size_t(vgahead):64;
        std::ofstream(folder+"/"+name,std::ios::binary)<<std::string(size,'\0');
    }
}
int main() {
    using wolf_content::WolfContent; using wolf_content::inspectWolf;
    WolfContent c;
    fixture("build/tests/gt","wl6",450,true); assert(inspectWolf("build/tests/gt",c) && c.supported && c.edition=="gt");
    std::string gt=c.saveKey;
    fixture("build/tests/apogee","wl6",486,false); assert(inspectWolf("build/tests/apogee",c) && c.supported && c.edition=="apogee");
    assert(c.saveKey!=gt);
    fixture("build/tests/shareware","wl1",471,true); assert(inspectWolf("build/tests/shareware",c) && c.supported && c.edition=="shareware");
    fixture("build/tests/unknown","wl6",999,false); assert(inspectWolf("build/tests/unknown",c) && !c.supported && !c.reason.empty());
    fs::remove(fs::path("build/tests/gt")/"VSWAP.WL6"); assert(!inspectWolf("build/tests/gt",c));
    fs::create_directories("build/tests/empty"); assert(!inspectWolf("build/tests/empty",c));
    printf("Unit: Wolfenstein 3D 1.4 GT/Activision, Apogee and shareware detection, distinct save profiles and refusals passed\n");
}
