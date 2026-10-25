// SPDX-License-Identifier: GPL-2.0-only
// MetalWolf's first process: finds the player's Wolfenstein 3D data, works out
// its edition and replaces itself with the engine built for that edition
// (Wolf-gt, Wolf-apogee or Wolf-shareware in the same app bundle).
#import <AppKit/AppKit.h>
#include "WolfContent.hpp"
#include <string>
#include <unistd.h>
#include <vector>

static NSString *homeFolder=nil;
// Settings and saves: Application Support/MetalWolf, or the folder given with
// --home (a portable or test setup).
static NSString *supportFolder() { return homeFolder?:[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/MetalWolf"]; }
static NSString *absolute(NSString *path) {
    path=path.stringByStandardizingPath;
    return path.isAbsolutePath?path:[NSFileManager.defaultManager.currentDirectoryPath stringByAppendingPathComponent:path];
}
static bool usable(NSString *folder,wolf_content::WolfContent &content) {
    return folder && wolf_content::inspectWolf(folder.fileSystemRepresentation,content) && content.supported;
}
// AppKit starts only for a panel or an alert: the engine replaces this
// process, and must inherit no AppKit signal handling.
static void appKit() {
    if(NSApp) return;
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}
static void alert(NSString *title,NSString *detail) {
    appKit();
    NSAlert *a=[NSAlert new]; a.messageText=title; a.informativeText=detail; [a runModal];
}
// The folder chosen before (remembered), the support folder, a known Steam
// location, or one the player selects.
static NSString *findData(wolf_content::WolfContent &content) {
    NSUserDefaults *prefs=NSUserDefaults.standardUserDefaults;
    NSMutableArray<NSString *> *candidates=[NSMutableArray array];
    if(NSString *saved=[prefs stringForKey:@"DataFolder"]) [candidates addObject:saved];
    [candidates addObject:supportFolder()];
    NSString *steam=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Steam/steamapps/common"];
    for(NSString *game in @[@"Wolfenstein 3D/base",@"Wolfenstein 3D/base/data",@"Wolfenstein 3D"]) [candidates addObject:[steam stringByAppendingPathComponent:game]];
    for(NSString *folder in candidates) if(usable(folder,content)) return folder;
    appKit();
    for(;;) {
        NSOpenPanel *panel=[NSOpenPanel openPanel];
        panel.canChooseFiles=NO; panel.canChooseDirectories=YES; panel.allowsMultipleSelection=NO;
        panel.title=@"Select the Wolfenstein 3D folder";
        panel.message=@"MetalWolf needs the game data from your copy of Wolfenstein 3D (the folder with VSWAP.WL6, or VSWAP.WL1 for the shareware episode). The files stay where they are.";
        panel.prompt=@"Play";
        if([panel runModal]!=NSModalResponseOK) return nil;
        NSString *folder=panel.URL.path;
        if(usable(folder,content)) { [prefs setObject:folder forKey:@"DataFolder"]; return folder; }
        alert(@"This folder cannot be used",[NSString stringWithFormat:@"%s\n\nSelect the folder of Wolfenstein 3D version 1.4: the full game (GT/Activision or Apogee) or the shareware episode.",
              content.reason.empty()?"No Wolfenstein 3D game data was found in it.":content.reason.c_str()]);
    }
}
int main(int argc,char **argv) { @autoreleasepool {
    NSString *data=nil;
    std::vector<std::string> engineOptions; // other options go to the engine (Wolf4SDL's own)
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--home") && i+1<argc) homeFolder=absolute([NSString stringWithUTF8String:argv[++i]]);
        else if(!strcmp(argv[i],"--data") && i+1<argc) data=absolute([NSString stringWithUTF8String:argv[++i]]);
        else if(strncmp(argv[i],"-psn_",5)) engineOptions.push_back(argv[i]);
    }
    wolf_content::WolfContent content;
    if(data) {
        if(!usable(data,content)) { fprintf(stderr,"%s\n",content.reason.empty()?"No supported Wolfenstein 3D data in that folder":content.reason.c_str()); return 1; }
    } else if(!(data=findData(content))) return 0;
    // Saves and settings per game data, and lowercase aliases of the data
    // files so case-sensitive volumes work; the player's files are untouched.
    NSString *save=[supportFolder() stringByAppendingPathComponent:[NSString stringWithFormat:@"profiles/%s",content.saveKey.c_str()]];
    NSString *alias=[save stringByAppendingPathComponent:@"data"];
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtPath:alias withIntermediateDirectories:YES attributes:nil error:&error]) { alert(@"MetalWolf could not start",error.localizedDescription); return 1; }
    if(save.length>250) { alert(@"MetalWolf could not start",@"The settings folder path is too long for the original engine."); return 1; }
    for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:data error:nil]) {
        if(![@[@"wl1",@"wl3",@"wl6"] containsObject:name.pathExtension.lowercaseString]) continue;
        NSString *link=[alias stringByAppendingPathComponent:name.lowercaseString],*target=[data stringByAppendingPathComponent:name];
        NSString *existing=[NSFileManager.defaultManager destinationOfSymbolicLinkAtPath:link error:nil];
        if(existing && ![existing isEqual:target]) [NSFileManager.defaultManager removeItemAtPath:link error:nil];
        if(![NSFileManager.defaultManager fileExistsAtPath:link]) [NSFileManager.defaultManager createSymbolicLinkAtPath:link withDestinationPath:target error:nil];
    }
    if(chdir(alias.fileSystemRepresentation)) { alert(@"MetalWolf could not start",@"The game data folder cannot be opened."); return 1; }
    NSString *engine=[NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:[NSString stringWithFormat:@"Wolf-%s",content.edition.c_str()]];
    std::string path=engine.fileSystemRepresentation,configdir=save.fileSystemRepresentation;
    std::vector<char *> arguments={path.data(),(char *)"--configdir",configdir.data()};
    for(auto &option:engineOptions) arguments.push_back(option.data());
    arguments.push_back(nullptr);
    execv(path.c_str(),arguments.data());
    fprintf(stderr,"Cannot start %s: %s\n",path.c_str(),strerror(errno));
    alert(@"MetalWolf could not start",[NSString stringWithFormat:@"The %s engine is missing from the app.",content.edition.c_str()]);
    return 1;
} }
