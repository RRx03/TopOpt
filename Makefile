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
            $(SRC)/topopt/SIMP3D.cpp $(SRC)/topopt/GridTransfer.cpp \
            $(SRC)/topopt/MMAOptimizer.cpp
# 2D axisymmetric Q4 FEM (Eigen only, no Metal) — Phase 4 step 6.
AXI_SRCS := $(SRC)/fem/AxiQ4Element.cpp $(SRC)/fem/FEM2DAxi.cpp
# Discrete thermo-elastic adjoint (Eigen only, no Metal) — Phase 4 validation.
ADJ_SRCS := $(SRC)/adjoint/ThermoElasticAdjoint.cpp
# Triple-coupled adjoint Stokes->CHT->thermo-elastic (Eigen only) — Phase 5 gate.
TRIADJ_SRCS := $(SRC)/adjoint/TripleAdjoint.cpp
# Incompressible Stokes Q1-Q1 PSPG solver (Eigen only, no Metal) — Phase 5.
STOKES_SRCS := $(SRC)/physics/StokesSolver.cpp
# CHT advection-diffusion + SUPG temperature solver (Eigen only, no Metal) — P5.
CHT_SRCS := $(SRC)/physics/CHTSolver.cpp
# Axisymmetric stress p-norm adjoint (Eigen only, no Metal) — Phase 4 step 7a.
AXIADJ_SRCS := $(SRC)/adjoint/AxiStressAdjoint.cpp
# Metal context core (device/queue/library + single metal-cpp impl TU).
GPU_CORE_SRCS := $(SRC)/gpu/MetalContext.cpp $(SRC)/gpu/metal_impl.cpp
# GPU solvers (matrix-free CG + Helmholtz filter), depend on CPU FEM core.
GPU_SOLVER_SRCS := $(SRC)/gpu/CGSolver3D.cpp $(SRC)/filter/Helmholtz3D.cpp \
                   $(SRC)/topopt/MultiGridOptimizer.cpp \
                   $(SRC)/physics/ThermalSolver.cpp
# IO.
IO_SRCS  := $(SRC)/io/STLExporter.cpp

CPU_OBJS      := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(CPU_SRCS))
AXI_OBJS      := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(AXI_SRCS))
ADJ_OBJS      := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(ADJ_SRCS))
TRIADJ_OBJS   := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(TRIADJ_SRCS))
STOKES_OBJS   := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(STOKES_SRCS))
CHT_OBJS      := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(CHT_SRCS))
AXIADJ_OBJS   := $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(AXIADJ_SRCS))
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
TEST_TE    := $(BUILD)/test_thermoelastic
TEST_ADJ   := $(BUILD)/test_adjoint_fd
TEST_STR   := $(BUILD)/test_stress
TEST_SADJ  := $(BUILD)/test_stress_adjoint_fd
TEST_MMA   := $(BUILD)/test_mma
NOZZLE_AXI := $(BUILD)/nozzle_axi
TEST_AXI   := $(BUILD)/test_axisymmetric
TEST_AXISADJ := $(BUILD)/test_axi_stress_adjoint_fd
TEST_STOKES := $(BUILD)/test_stokes
TEST_BRINK  := $(BUILD)/test_brinkman
TEST_CHT   := $(BUILD)/test_cht
TEST_TRIADJ := $(BUILD)/test_triple_adjoint_fd
COOLING_JACKET := $(BUILD)/cooling_jacket
TOPOPT     := $(BUILD)/topopt

.PHONY: all test test_cpu run clean
all: $(TEST_HELLO) $(TEST_FEM) $(TEST_CG) $(TEST_MBB) $(TEST_MG) $(TEST_TH) $(TEST_TE) $(TEST_ADJ) $(TEST_STR) $(TEST_SADJ) $(TEST_MMA) $(TEST_AXI) $(TEST_AXISADJ) $(TEST_STOKES) $(TEST_BRINK) $(TEST_CHT) $(TEST_TRIADJ) $(TOPOPT) $(METALLIB)

# --- link rules ---
$(TEST_HELLO): $(GPU_CORE_OBJS) $(OBJ)/test_metal_hello.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_FEM): $(CPU_OBJS) $(OBJ)/test_fem3d.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# CPU-pure (Eigen only, no Metal frameworks): the Phase 4 adjoint gate.
$(TEST_ADJ): $(CPU_OBJS) $(ADJ_OBJS) $(OBJ)/test_adjoint_fd.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: stress model (von Mises + qp-relaxation + p-norm).
$(TEST_STR): $(CPU_OBJS) $(OBJ)/test_stress.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: stress p-norm adjoint gate (Phase 4, second gate).
$(TEST_SADJ): $(CPU_OBJS) $(ADJ_OBJS) $(OBJ)/test_stress_adjoint_fd.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: MMA optimiser gate (Phase 4) — oracle A (analytic) + oracle B (vs OC).
$(TEST_MMA): $(CPU_OBJS) $(OBJ)/test_mma.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: 2D axisymmetric Q4 FEM gate (Phase 4) — Lame thick-cylinder oracle.
$(TEST_AXI): $(AXI_OBJS) $(OBJ)/test_axisymmetric.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: axisymmetric stress p-norm adjoint gate (Phase 4 step 7a) — FD oracle.
# CPU-pure axisymmetric structural nozzle TO demo (step 7b) + revolved STL.
$(NOZZLE_AXI): $(CPU_OBJS) $(AXI_OBJS) $(AXIADJ_OBJS) $(IO_OBJS) $(OBJ)/apps/nozzle_axi.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_AXISADJ): $(AXI_OBJS) $(AXIADJ_OBJS) $(OBJ)/test_axi_stress_adjoint_fd.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: incompressible Stokes Q1-Q1 PSPG gate (Phase 5) — Poiseuille oracle.
$(TEST_STOKES): $(STOKES_OBJS) $(OBJ)/test_stokes.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: Brinkman penalization gate (Phase 5) — Darcy-Brinkman + non-leak.
$(TEST_BRINK): $(STOKES_OBJS) $(OBJ)/test_brinkman.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: CHT advection-diffusion + SUPG gate (Phase 5) — 1D analytic oracle.
$(TEST_CHT): $(CHT_OBJS) $(OBJ)/test_cht.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: TRIPLE-coupled adjoint gate (Phase 5, hardest) — FD oracle < 1e-3.
$(TEST_TRIADJ): $(OBJ)/fem/H8Element.o $(TRIADJ_OBJS) $(OBJ)/test_triple_adjoint_fd.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# CPU-pure: end-to-end multiphysics TO demo (Phase 5) — MMA + TripleAdjoint +
# 3D density filter + Heaviside continuation. Produces output/cooling_jacket.vti.
$(COOLING_JACKET): $(CPU_OBJS) $(TRIADJ_OBJS) $(OBJ)/apps/cooling_jacket.o
	$(CXX) $(CXXFLAGS) $^ -o $@

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

$(TEST_TE): $(CPU_OBJS) $(GPU_OBJS) $(OBJ)/test_thermoelastic.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# Full GPU test suite (needs the metallib).
test: all
	./$(TEST_FEM)
	./$(TEST_MG)
	./$(TEST_ADJ)
	./$(TEST_STR)
	./$(TEST_SADJ)
	./$(TEST_MMA)
	./$(TEST_AXI)
	./$(TEST_AXISADJ)
	./$(TEST_STOKES)
	./$(TEST_BRINK)
	./$(TEST_CHT)
	./$(TEST_TRIADJ)
	./$(TEST_TH)
	./$(TEST_TE)
	./$(TEST_CG)
	./$(TEST_HELLO)
	./$(TEST_MBB)

# CPU-only checks (no GPU / no metallib needed).
test_cpu: $(TEST_FEM) $(TEST_MG) $(TEST_ADJ) $(TEST_STR) $(TEST_SADJ) $(TEST_MMA) $(TEST_AXI) $(TEST_AXISADJ) $(TEST_STOKES) $(TEST_BRINK) $(TEST_CHT) $(TEST_TRIADJ)
	./$(TEST_FEM)
	./$(TEST_MG)
	./$(TEST_ADJ)
	./$(TEST_STR)
	./$(TEST_SADJ)
	./$(TEST_MMA)
	./$(TEST_AXI)
	./$(TEST_AXISADJ)
	./$(TEST_STOKES)
	./$(TEST_BRINK)
	./$(TEST_CHT)
	./$(TEST_TRIADJ)

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
	       $(TEST_TH) $(TEST_TE) $(TEST_ADJ) $(TEST_STR) $(TEST_SADJ) \
	       $(TEST_MMA) $(TEST_AXI) $(TEST_AXISADJ) $(TEST_STOKES) \
	       $(TEST_BRINK) $(TEST_CHT) $(TEST_TRIADJ) $(COOLING_JACKET) \
	       $(TOPOPT) $(METAL_AIR) $(METALLIB)
