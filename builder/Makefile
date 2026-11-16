# Joy Launcher 테마 빌더 (리눅스: make / 윈도우 교차 빌드: make WIN=1)
APP_VERSION := $(shell sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' ../Makefile)
IMGUI = third_party/imgui
SRC_APP = main.cpp stubs.cpp
SRC_JOY = ../source/main.cpp ../source/layout.cpp ../source/pegasus.cpp ../source/hbmenu.cpp ../source/i18n.cpp \
          ../source/installed.cpp ../source/debuglog.cpp ../source/esde.cpp
SRC_IMGUI = $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
            $(IMGUI)/backends/imgui_impl_sdl2.cpp $(IMGUI)/backends/imgui_impl_sdlrenderer2.cpp $(IMGUI)/misc/cpp/imgui_stdlib.cpp
ifdef WIN
  SDLW ?= third_party/sdl-mingw
  CXX = x86_64-w64-mingw32-g++-posix
  OUT = build-win/JoyThemeBuilder.exe
  OBJDIR = build-win/obj
  CXXFLAGS_SDL = -I$(SDLW)/include/SDL2 -I$(SDLW)/include -include wincompat.h
  LIBS = -L$(SDLW)/lib -lmingw32 -lSDL2main -lSDL2_ttf -lSDL2_image -lSDL2 -lws2_32 -mwindows -static-libgcc -static-libstdc++ \
         -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
else
  CXX = g++
  OUT = build/joythemebuilder
  OBJDIR = build/obj
  CXXFLAGS_SDL = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
  LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)
endif
CXXFLAGS = -O2 -std=gnu++17 -DJOY_BUILDER -DAPP_VERSION_STR=\"$(APP_VERSION)\" -I$(IMGUI) -I$(IMGUI)/backends $(CXXFLAGS_SDL)
SRCS = $(SRC_APP) $(SRC_JOY) $(SRC_IMGUI)
OBJS = $(addprefix $(OBJDIR)/,$(subst ../,up_,$(subst /,_,$(SRCS:.cpp=.o))))

all: $(OUT)

ifdef WIN
RES = $(OBJDIR)/app_res.o
$(RES): app.rc app.manifest app.ico
	@mkdir -p $(OBJDIR)
	x86_64-w64-mingw32-windres app.rc -O coff -o $@
endif

$(OUT): $(OBJS) $(RES)
	$(CXX) -o $@ $(OBJS) $(RES) $(LIBS)

define RULE
$(OBJDIR)/$(subst ../,up_,$(subst /,_,$(1:.cpp=.o))): $(1) $(wildcard *.h) $(wildcard ../source/*.h) joy_api.inc
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(1) -o $$@
endef
$(foreach s,$(SRCS),$(eval $(call RULE,$(s))))

# 데모 창: 런처 그대로 (영상·네트워크·BGM 은 빈 함수), 키보드로 조작
DEMO_OUT = $(if $(WIN),build-win/JoyDemo.exe,build/joydemo)
DEMO_SRCS = stubs.cpp ../source/main.cpp ../source/layout.cpp ../source/pegasus.cpp ../source/hbmenu.cpp ../source/i18n.cpp \
            ../source/installed.cpp ../source/debuglog.cpp ../source/esde.cpp
DEMO_FLAGS = -O2 -std=gnu++17 -DAPP_VERSION_STR=\"$(APP_VERSION)\" $(CXXFLAGS_SDL)
demo: $(DEMO_OUT)
$(DEMO_OUT): $(DEMO_SRCS) $(wildcard ../source/*.h) $(RES)
	@mkdir -p $(dir $@)
	$(CXX) $(DEMO_FLAGS) -o $@ $(DEMO_SRCS) $(RES) $(LIBS)

clean:
	rm -rf build build-win
