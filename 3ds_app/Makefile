# Explicit devkitARM/libctru/citro2d build with standard SMDH/3DSX packaging.

TARGET          := 3ds_autoui
BUILD           := build
SOURCES         := source
APP_TITLE       := 3DS AutoUI
APP_DESCRIPTION := Branded Subaru WRX OBD2 dashboard
APP_AUTHOR      := 3DS AutoUI
APP_ICON        := assets/autoui_icon.png
ROMFS           := romfs
LOGO_T3X        := $(ROMFS)/gfx/autoui_brand.t3x

ARCH            := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS          := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
CXXFLAGS        := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
LDFLAGS         := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections
LIBS            := -lcitro2d -lcitro3d -lctru -lm

include $(DEVKITARM)/3ds_rules

CPPFILES        := $(wildcard $(SOURCES)/*.cpp)
OBJECTS         := $(patsubst $(SOURCES)/%.cpp,$(BUILD)/%.o,$(CPPFILES))
DEPSDIR         := $(BUILD)
CPPFLAGS        += -I$(SOURCES) -I$(DEVKITPATH)/libctru/include -D__3DS__
LDFLAGS         += -L$(DEVKITPATH)/libctru/lib
LD              := $(CXX)

.PHONY: all clean release

all: $(TARGET).3dsx

release: clean all

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.cpp | $(BUILD)
	@echo "CXX $<"
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	@echo "LD $@"
	@$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).smdh: $(APP_ICON) Makefile
	@echo "SMDH $@"
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(LOGO_T3X): assets/autoui_brand.t3s assets/autoui_brand.png
	@echo "TEX3DS $@"
	@mkdir -p $(ROMFS)/gfx
	@cd assets && tex3ds -i autoui_brand.t3s -o ../$(ROMFS)/gfx/autoui_brand.t3x

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh $(LOGO_T3X)
	@echo "3DSX $@"
	@3dsxtool $< $@ --smdh=$(TARGET).smdh --romfs=$(ROMFS)

clean:
	@rm -rf $(BUILD) $(ROMFS) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

-include $(OBJECTS:.o=.d)
