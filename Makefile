# TopOpt Phase 2 — Metal GPU foundation
# clang++ / C++23 / metal-cpp (macOS Apple Silicon)

CXX      := clang++
CXXSTD   := -std=c++23
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2
# Our code: full warnings (-I). metal-cpp: -isystem to silence vendor warnings.
# -fno-objc-arc is required: metal-cpp does its own manual ref counting.
INCLUDES := -Isrc -isystem third_party/metal-cpp
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(INCLUDES) -fno-objc-arc
LDFLAGS  := -framework Metal -framework Foundation -framework QuartzCore

BUILD   := build
OBJ     := build/obj
SRC     := src
SHADERS := shaders

# C++ modules (Metal context + the single metal-cpp implementation TU)
LIB_SRCS := $(SRC)/gpu/MetalContext.cpp $(SRC)/gpu/metal_impl.cpp
LIB_OBJS := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(LIB_SRCS))
TEST_OBJ := $(OBJ)/test_metal_hello.o
TEST     := $(BUILD)/test_metal_hello

# Metal shaders -> AIR -> single .metallib
METAL_SRCS := $(wildcard $(SHADERS)/*.metal)
METAL_AIR  := $(patsubst $(SHADERS)/%.metal,$(BUILD)/%.air,$(METAL_SRCS))
METALLIB   := $(BUILD)/shaders.metallib

.PHONY: all test run clean

all: $(TEST) $(METALLIB)

$(TEST): $(LIB_OBJS) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test: all
	./$(TEST)

run: test

# C++ compile (auto-creates build/obj/<module>/ subdirs)
$(OBJ)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/test_metal_hello.o: tests/test_metal_hello.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Shader compile: .metal -> .air -> .metallib
$(BUILD)/%.air: $(SHADERS)/%.metal
	@mkdir -p $(BUILD)
	xcrun -sdk macosx metal -c $< -o $@

$(METALLIB): $(METAL_AIR)
	xcrun -sdk macosx metallib $^ -o $@

clean:
	rm -rf $(OBJ) $(TEST) $(METAL_AIR) $(METALLIB)
