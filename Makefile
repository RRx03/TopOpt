# TopOpt Phase 3 — multi-grid + mesh independence (on the Phase 2 matrix-free base)
# clang++ / C++23 / metal-cpp (macOS Apple Silicon)

CXX      := clang++
CXXSTD   := -std=c++23
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O3 -DEIGEN_NO_DEBUG -DNDEBUG
# Our code: full warnings (-I). Vendored deps: -isystem to silence them.
# -fno-objc-arc is required: metal-cpp does its own manual ref counting.
INCLUDES := -Isrc -isystem third_party/metal-cpp -isystem third_party/eigen \
            -isystem third_party
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(INCLUDES) -fno-objc-arc
LDFLAGS  := -framework Metal -framework Foundation -framework QuartzCore

BUILD   := build
OBJ     := build/obj
SRC     := src
SHADERS := shaders

# CPU FEM core (Eigen only, no Metal).
CPU_SRCS := $(SRC)/fem/H8Element.cpp $(SRC)/fem/FEM3D.cpp \
            $(SRC)/topopt/SIMP3D.cpp $(SRC)/topopt/GridTransfer.cpp
# Metal context core (device/queue/library + single metal-cpp impl TU).
GPU_CORE_SRCS := $(SRC)/gpu/MetalContext.cpp $(SRC)/gpu/metal_impl.cpp
# GPU solvers (matrix-free CG + Helmholtz filter), depend on CPU FEM core.
GPU_SOLVER_SRCS := $(SRC)/gpu/CGSolver3D.cpp $(SRC)/filter/Helmholtz3D.cpp \
                   $(SRC)/topopt/MultiGridOptimizer.cpp \
                   $(SRC)/physics/ThermalSolver.cpp
# IO.
IO_SRCS  := $(SRC)/io/STLExporter.cpp

CPU_OBJS      := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(CPU_SRCS))
GPU_CORE_OBJS := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(GPU_CORE_SRCS))
GPU_OBJS      := $(GPU_CORE_OBJS) \
                 $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(GPU_SOLVER_SRCS))
IO_OBJS       := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(IO_SRCS))

# Metal shaders -> AIR -> single .metallib
METAL_SRCS := $(wildcard $(SHADERS)/*.metal)
METAL_AIR  := $(patsubst $(SHADERS)/%.metal,$(BUILD)/%.air,$(METAL_SRCS))
METALLIB   := $(BUILD)/shaders.metallib

# Binaries
TEST_HELLO := $(BUILD)/test_metal_hello
TEST_FEM   := $(BUILD)/test_fem3d
TEST_CG    := $(BUILD)/test_cg_gpu
TEST_MBB   := $(BUILD)/test_mbb3d
TEST_MG    := $(BUILD)/test_multigrid
TEST_TH    := $(BUILD)/test_thermal
TOPOPT     := $(BUILD)/topopt

.PHONY: all test test_cpu run clean
all: $(TEST_HELLO) $(TEST_FEM) $(TEST_CG) $(TEST_MBB) $(TEST_MG) $(TEST_TH) $(TOPOPT) $(METALLIB)

# --- link rules ---
$(TEST_HELLO): $(GPU_CORE_OBJS) $(OBJ)/test_metal_hello.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_FEM): $(CPU_OBJS) $(OBJ)/test_fem3d.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_CG): $(CPU_OBJS) $(GPU_OBJS) $(OBJ)/test_cg_gpu.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_MBB): $(CPU_OBJS) $(GPU_OBJS) $(IO_OBJS) $(OBJ)/test_mbb3d.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TOPOPT): $(CPU_OBJS) $(GPU_OBJS) $(IO_OBJS) $(OBJ)/main.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_MG): $(CPU_OBJS) $(OBJ)/test_multigrid.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_TH): $(CPU_OBJS) $(GPU_OBJS) $(OBJ)/test_thermal.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# Full GPU test suite (needs the metallib).
test: all
	./$(TEST_FEM)
	./$(TEST_MG)
	./$(TEST_TH)
	./$(TEST_CG)
	./$(TEST_HELLO)
	./$(TEST_MBB)

# CPU-only checks (no GPU / no metallib needed).
test_cpu: $(TEST_FEM) $(TEST_MG)
	./$(TEST_FEM)
	./$(TEST_MG)

run: $(TOPOPT) $(METALLIB)
	./$(TOPOPT) mbb

# --- compile rules ---
$(OBJ)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Shader compile: .metal -> .air -> .metallib
$(BUILD)/%.air: $(SHADERS)/%.metal
	@mkdir -p $(BUILD)
	xcrun -sdk macosx metal -c $< -o $@

$(METALLIB): $(METAL_AIR)
	xcrun -sdk macosx metallib $^ -o $@

clean:
	rm -rf $(OBJ) $(TEST_HELLO) $(TEST_FEM) $(TEST_CG) $(TEST_MBB) $(TEST_MG) \
	       $(TEST_TH) $(TOPOPT) $(METAL_AIR) $(METALLIB)
