// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <AppKit/AppKit.h>
#include <cmath>
// Native Spaces fullscreen on ordinary displays; view fullscreen across the
// entire display (notch area included) on a notched screen.
// The caller retains its window delegate and handles engine input separately.
class NativeFullscreen {
    NSWindow *__strong windowedWindow=nil;
    NSView *__strong fullscreenView=nil;
    NSApplicationPresentationOptions windowedOptions=NSApplicationPresentationDefault;
public:
    bool transitioning=false;
    bool active(NSWindow *window) const { return windowedWindow || (window.styleMask&NSWindowStyleMaskFullScreen); }
    NSWindow *toggle(NSWindow *window,id<NSWindowDelegate> delegate) {
        if(transitioning)return window;
        if(windowedWindow) {
            [fullscreenView exitFullScreenModeWithOptions:nil];
            window=windowedWindow;windowedWindow=nil;fullscreenView=nil;
            NSApp.presentationOptions=windowedOptions;
            [window makeKeyAndOrderFront:nil];[window makeFirstResponder:window.contentView];
        } else if(NSScreen *screen=window.screen?:NSScreen.mainScreen; screen.safeAreaInsets.top>0 && !(window.styleMask&NSWindowStyleMaskFullScreen)) {
            NSView *view=window.contentView;NSWindow *original=window;
            windowedOptions=NSApp.presentationOptions;
            NSApp.presentationOptions=NSApplicationPresentationHideDock|NSApplicationPresentationHideMenuBar;
            if([view enterFullScreenMode:screen withOptions:@{NSFullScreenModeAllScreens:@NO,NSFullScreenModeApplicationPresentationOptions:@(NSApp.presentationOptions)}]) {
                windowedWindow=original;fullscreenView=view;window=view.window;
                window.delegate=delegate;window.opaque=YES;window.backgroundColor=NSColor.blackColor;window.hasShadow=NO;
                window.acceptsMouseMovedEvents=YES;[window setFrame:screen.frame display:YES];
                [original orderOut:nil];[window makeKeyAndOrderFront:nil];[window makeFirstResponder:view];
            } else NSApp.presentationOptions=windowedOptions;
        } else { transitioning=true;[window toggleFullScreen:nil]; }
        return window;
    }
    void shutdown(NSWindow *window) {
        if(fullscreenView) { [fullscreenView exitFullScreenModeWithOptions:nil];NSApp.presentationOptions=windowedOptions;[windowedWindow orderOut:nil];windowedWindow=nil;fullscreenView=nil; }
        [window orderOut:nil];
    }
    static NSApplicationPresentationOptions options(NSApplicationPresentationOptions value) {
        return (value&~(NSApplicationPresentationAutoHideMenuBar|NSApplicationPresentationAutoHideDock))|NSApplicationPresentationHideMenuBar|NSApplicationPresentationHideDock;
    }
};
