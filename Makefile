SHELL := /bin/sh
SDK := $(shell xcrun --sdk macosx --show-sdk-path)
CC := xcrun clang
CXX := xcrun clang++
CONFIG ?= release
BUILD ?= build/$(CONFIG)
ifeq ($(CONFIG),release)
OPT := -O3 -mcpu=apple-m1 -flto=full
else ifeq ($(CONFIG),debug)
OPT := -O0
else ifeq ($(CONFIG),sanitize)
OPT := -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
else
$(error CONFIG must be release, debug or sanitize)
endif
APP := $(BUILD)/MetalWolf.app
VERSION := $(shell /usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' src/platform/Info.plist)
DMG := MetalWolf-$(VERSION)-macOS-arm64.dmg
MACOS := $(APP)/Contents/MacOS
RESOURCES := $(APP)/Contents/Resources
COMMON := -arch arm64 -isysroot $(SDK) -mmacosx-version-min=13.0 -g $(OPT)
WOLF := $(COMMON) -Isrc/wolf -Isrc/platform -DVERSIONALREADYCHOSEN -DCARMACIZED -D__MACOSX__
FRAMEWORKS := -framework AppKit -framework Metal -framework QuartzCore -framework CoreGraphics -framework AudioToolbox -framework CoreAudio -framework GameController -framework CoreHaptics
# One engine per Wolfenstein 3D 1.4 edition; the launcher picks it.
VARIANTS := gt apogee shareware
FLAGS_gt := -DGOODTIMES
FLAGS_apogee :=
FLAGS_shareware := -DUPLOAD
CORE := $(wildcard src/wolf/*.cpp)
PLATFORM := src/platform/App.mm src/platform/Audio.mm src/platform/Software.cpp src/platform/Presentation.cpp src/platform/Gore.cpp \
	src/platform/Feedback.cpp src/platform/GoreMasks.cpp src/platform/GoreSound.cpp src/platform/Rumble.mm src/platform/PresentationMetrics.mm src/platform/Parallel.cpp
POBJS := $(patsubst src/platform/%,$(BUILD)/platform/%.o,$(PLATFORM))
.DELETE_ON_ERROR:
.DEFAULT_GOAL := all
.PHONY: all test check release dmg

all: $(MACOS)/MetalWolf $(foreach v,$(VARIANTS),$(MACOS)/Wolf-$(v)) $(RESOURCES)/wolf.metallib $(APP)/Contents/Info.plist $(APP)/Contents/PkgInfo \
	$(RESOURCES)/LICENSE $(RESOURCES)/LICENSE-LGPL-2.1.txt $(RESOURCES)/NOTICE.txt $(RESOURCES)/AppIcon.icns

# The platform layer is shared by every edition (built with the GT layout, as
# its code does not depend on the edition's graphics tables).
$(BUILD)/platform/%.mm.o: src/platform/%.mm Makefile
	@mkdir -p $(@D)
	@echo CXX $<
	@$(CXX) $(WOLF) -std=c++17 -fobjc-arc -DGOODTIMES -MMD -MP -c $< -o $@
$(BUILD)/platform/%.cpp.o: src/platform/%.cpp Makefile
	@mkdir -p $(@D)
	@echo CXX $<
	@$(CXX) $(WOLF) -std=c++17 -DGOODTIMES -MMD -MP -c $< -o $@
LAUNCHER := $(BUILD)/launcher/Launcher.o $(BUILD)/launcher/WolfContent.o
$(BUILD)/launcher/%.o: src/platform/%.mm Makefile
	@mkdir -p $(@D)
	@echo CXX $<
	@$(CXX) $(COMMON) -std=c++17 -fobjc-arc -Isrc/platform -MMD -MP -c $< -o $@
$(BUILD)/launcher/%.o: src/platform/%.cpp Makefile
	@mkdir -p $(@D)
	@echo CXX $<
	@$(CXX) $(COMMON) -std=c++17 -Isrc/platform -MMD -MP -c $< -o $@
$(MACOS)/MetalWolf: $(LAUNCHER)
	@mkdir -p $(@D)
	@echo LINK $@
	@$(CXX) $(COMMON) $^ -framework AppKit -o $@

define VARIANT_RULES
OBJS_$(1) := $$(patsubst src/wolf/%.cpp,$$(BUILD)/$(1)/%.o,$$(CORE)) $$(BUILD)/$(1)/opl3.o
$$(BUILD)/$(1)/%.o: src/wolf/%.cpp Makefile
	@mkdir -p $$(@D)
	@echo CXX $$< [$(1)]
	@$$(CXX) $$(WOLF) -std=c++17 $$(FLAGS_$(1)) -Wno-deprecated -MMD -MP -c $$< -o $$@
$$(BUILD)/$(1)/opl3.o: src/wolf/opl3.c Makefile
	@mkdir -p $$(@D)
	@$$(CC) $$(WOLF) $$(FLAGS_$(1)) -MMD -MP -c $$< -o $$@
$$(MACOS)/Wolf-$(1): $$(OBJS_$(1)) $$(POBJS)
	@mkdir -p $$(@D)
	@echo LINK $$@
	@$$(CXX) $$(COMMON) $$^ $$(FRAMEWORKS) -o $$@
-include $$(OBJS_$(1):.o=.d)
endef
$(foreach v,$(VARIANTS),$(eval $(call VARIANT_RULES,$(v))))

$(BUILD)/wolf.air: shaders/wolf.metal
	@mkdir -p $(@D)
	xcrun -sdk macosx metal -std=metal3.0 -mmacosx-version-min=13.0 -fmodules-cache-path=$(abspath $(BUILD))/metal-modules -c $< -o $@
$(RESOURCES)/wolf.metallib: $(BUILD)/wolf.air
	@mkdir -p $(@D)
	xcrun -sdk macosx metallib $^ -o $@
$(APP)/Contents/Info.plist: src/platform/Info.plist
	@mkdir -p $(@D)
	cp $< $@
# The app icon: an original picture drawn by tools/icon.mm.
$(BUILD)/icon: tools/icon.mm
	@mkdir -p $(@D)
	$(CXX) -arch arm64 -isysroot $(SDK) -mmacosx-version-min=13.0 -std=c++17 -fobjc-arc $< -framework Foundation -framework CoreGraphics -framework ImageIO -o $@
$(RESOURCES)/AppIcon.icns: $(BUILD)/icon
	@mkdir -p $(@D)
	$(BUILD)/icon $(BUILD)/AppIcon.iconset
	iconutil -c icns $(BUILD)/AppIcon.iconset -o $@
$(APP)/Contents/PkgInfo:
	@mkdir -p $(@D)
	printf 'APPL????' > $@
$(RESOURCES)/%: %
	@mkdir -p $(@D)
	cp $< $@

$(BUILD)/tests/unit: tests/unit.cpp src/platform/WolfContent.cpp src/platform/WolfContent.hpp
	@mkdir -p $(@D)
	$(CXX) -arch arm64 -isysroot $(SDK) -mmacosx-version-min=13.0 -Isrc/platform -std=c++17 -O1 -g -fsanitize=address,undefined tests/unit.cpp src/platform/WolfContent.cpp -o $@
test: all $(BUILD)/tests/unit
	$(BUILD)/tests/unit

# Checks that need no GPU, audio device or display (continuous integration).
check: test

# An ad-hoc signed app bundle for sharing (not notarized).
release: all
	codesign --force --sign - $(APP)
	codesign --verify --strict $(APP)

# The download: a disk image with the signed app and a link to Applications.
dmg: release
	@mkdir -p $(BUILD)/dmg
	ditto $(APP) $(BUILD)/dmg/MetalWolf.app
	ln -sfn /Applications $(BUILD)/dmg/Applications
	hdiutil create -volname MetalWolf -srcfolder $(BUILD)/dmg -fs HFS+ -format UDZO -ov $(BUILD)/$(DMG)
	cd $(BUILD) && shasum -a 256 $(DMG) > $(DMG).sha256

-include $(POBJS:.o=.d) $(LAUNCHER:.o=.d)
