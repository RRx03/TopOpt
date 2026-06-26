# TopOpt — Phase 1 : MBB beam SIMP solver
# clang++ / C++23 / macOS Apple Silicon

CXX      := clang++
CXXSTD   := -std=c++23
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O3 -DEIGEN_NO_DEBUG -DNDEBUG
# Our code: full warnings (-I). Vendored deps: -isystem to silence their warnings.
INCLUDES := -Isrc -isystem third_party/eigen -isystem third_party -isystem third_party/stb
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(INCLUDES)

BUILD := build
OBJ   := build/obj
SRC   := src

# Library modules (everything except main.cpp)
LIB_SRCS := \
	$(SRC)/core/Grid2D.cpp \
	$(SRC)/fem/FEM2D.cpp \
	$(SRC)/topopt/SIMP.cpp \
	$(SRC)/filter/Helmholtz.cpp \
	$(SRC)/io/PNGWriter.cpp

LIB_OBJS := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(LIB_SRCS))
MAIN_OBJ := $(OBJ)/main.o
TEST_OBJ := $(OBJ)/test_mbb_beam.o

TOPOPT := $(BUILD)/topopt
TEST   := $(BUILD)/test_mbb

.PHONY: all test run clean

all: $(TOPOPT)

$(TOPOPT): $(LIB_OBJS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST): $(LIB_OBJS) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(TEST)
	./$(TEST)

run: $(TOPOPT)
	./$(TOPOPT) mbb

# Generic compile rule (auto-creates per-module subdirs in build/obj/)
$(OBJ)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Test object lives under tests/
$(OBJ)/test_mbb_beam.o: tests/test_mbb_beam.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ) $(TOPOPT) $(TEST)
