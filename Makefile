.PHONY: all graph tests benchmark run run_tests run_benchmark check clean

CXXFLAGS ?= -O2
GRAPH_FLAGS = -std=c++17 -Wall -Wextra -Wpedantic

all: gui

graph: gui
tests: build/tests
benchmark: build/benchmark

build:
	mkdir -p build

build/tests: tests/tests.cpp | build
	$(CXX) $(CPPFLAGS) $(GRAPH_FLAGS) $(CXXFLAGS) -MMD -MP -MF $@.d $< $(LDFLAGS) -o $@

build/benchmark: benchmarks/benchmark.cpp | build
	$(CXX) $(CPPFLAGS) $(GRAPH_FLAGS) $(CXXFLAGS) -MMD -MP -MF $@.d $< $(LDFLAGS) -o $@

build/tests_sanitized: tests/tests.cpp | build
	$(CXX) $(CPPFLAGS) $(GRAPH_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -MMD -MP -MF $@.d $< $(LDFLAGS) -o $@

run: run_gui

run_tests: build/tests
	./build/tests

run_benchmark: build/benchmark
	./build/benchmark

check: build/tests_sanitized
	./build/tests_sanitized

clean:
	rm -rf build

GUI_APP = build/Interplanetary\ Network.app
GUI_BIN = build/interplanetary_network
IMGUI_DIR = gui/third_party/imgui
GLFW_DIR = gui/third_party/glfw
GUI_INCLUDES = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(GLFW_DIR)/include
GUI_FRAMEWORKS = -framework Metal -framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore -framework UniformTypeIdentifiers
GUI_OBJS = build/gui/main.o build/gui/GuiApp.o build/gui/GraphView.o build/gui/GraphCanvas.o build/gui/BenchmarkView.o
IMGUI_OBJS = $(addprefix build/gui/imgui/,$(addsuffix .o,imgui imgui_draw imgui_tables imgui_widgets misc/cpp/imgui_stdlib backends/imgui_impl_glfw backends/imgui_impl_metal))

.PHONY: gui run_gui

gui: $(GUI_APP)

run_gui: gui
	open "$(subst \,,$(GUI_APP))"

$(GUI_APP): $(GUI_BIN) gui/assets/Info.plist gui/assets/DroidSans.ttf gui/assets/FONT-LICENSE.txt
	mkdir -p "$@/Contents/MacOS" "$@/Contents/Resources"
	cp $(GUI_BIN) "$@/Contents/MacOS/Interplanetary Network"
	cp gui/assets/Info.plist "$@/Contents/Info.plist"
	cp gui/assets/DroidSans.ttf gui/assets/FONT-LICENSE.txt "$@/Contents/Resources/"
	touch "$@"

$(GUI_BIN): $(GUI_OBJS) $(IMGUI_OBJS) $(GLFW_DIR)/lib/libglfw3.a
	$(CXX) $(LDFLAGS) $^ $(GUI_FRAMEWORKS) -o $@

build/gui/%.o: gui/%.cpp
	mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(GRAPH_FLAGS) $(CXXFLAGS) $(GUI_INCLUDES) -MMD -MP -c "$<" -o "$@"

build/gui/%.o: gui/%.mm
	mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(GRAPH_FLAGS) $(CXXFLAGS) $(GUI_INCLUDES) -fobjc-arc -MMD -MP -c "$<" -o "$@"

build/gui/imgui/%.o: $(IMGUI_DIR)/%.cpp
	mkdir -p "$(@D)"
	$(CXX) -std=c++17 $(CXXFLAGS) $(GUI_INCLUDES) -MMD -MP -c "$<" -o "$@"

build/gui/imgui/%.o: $(IMGUI_DIR)/%.mm
	mkdir -p "$(@D)"
	$(CXX) -std=c++17 $(CXXFLAGS) $(GUI_INCLUDES) -fobjc-arc -MMD -MP -c "$<" -o "$@"

-include $(GUI_OBJS:.o=.d) $(IMGUI_OBJS:.o=.d)

-include $(wildcard build/*.d)
