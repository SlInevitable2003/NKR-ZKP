.DEFAULT_GOAL := help

BUILD_DIR ?= build
GROTH16_BUILD_DIR ?= $(BUILD_DIR)/groth16
GROTH16_BIN_DIR ?= $(GROTH16_BUILD_DIR)/bin
GROTH16_DATA_DIR ?= $(GROTH16_BUILD_DIR)/data
JOBS ?= $(shell nproc)
THREADS ?= 4
MATRIX_SIZE ?= 4
PROFILE ?= 0
CUDA_ARCH ?= 120
DCU_ARCH ?=
CUDA_HOME ?= /usr/local/cuda
GROTH16_CPU_MATRIX_SIZE ?= 4
GROTH16_CUDA_MATRIX_SIZE ?= 12
GROTH16_DCU_MATRIX_SIZE ?= 12
GROTH16_DEPS_DIR ?= $(CURDIR)/protocols/groth16/third_party/depends
PROFILE_FLAG = $(if $(filter 1 true yes on,$(PROFILE)),--profile,)
DCU_ARCH_FLAG = $(if $(strip $(DCU_ARCH)),-DCMAKE_HIP_ARCHITECTURES=$(DCU_ARCH),)

.PHONY: help check groth16-cpu groth16-cuda groth16-dcu groth16-test-cpu groth16-test-cuda groth16-test-dcu groth16-clean plonk-cpu plonk-test plonk-cuda plonk-test-cuda stark stark-matmul clean

help:
	@echo "NKR-ZKP targets:"
	@echo "  check          validate repository layout"
	@echo "  groth16-cpu    configure and build the CPU backend"
	@echo "  groth16-cuda   configure and build the NVIDIA CUDA backend"
	@echo "  groth16-dcu    configure and build the HIP/DCU backend"
	@echo "  groth16-test-cpu   run a small CPU setup/prove/verify flow"
	@echo "  groth16-test-cuda  run a CUDA setup/prove/verify flow"
	@echo "  groth16-test-dcu   run a DCU setup/prove/verify flow"
	@echo "  groth16-clean  remove all Groth16 CMake build trees"
	@echo "  plonk-cpu      build the CPU PLONK snapshot"
	@echo "  plonk-test     run a small PLONK CPU proof"
	@echo "  plonk-cuda     build the experimental CUDA PLONK snapshot"
	@echo "  plonk-test-cuda  run the small PLONK CUDA proof example"
	@echo "  stark          build Winterfell with concurrent CPU support"
	@echo "  stark-matmul   run the Winterfell matrix multiplication example"

check:
	@./scripts/check-layout.sh

groth16-cpu:
	cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_GROTH16_ACCELERATOR=NONE \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)"
	cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-cuda:
	PATH="$(CUDA_HOME)/bin:$$PATH" CUDACXX="$(CUDA_HOME)/bin/nvcc" cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_GROTH16_ACCELERATOR=CUDA \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)" \
		-DCMAKE_CUDA_ARCHITECTURES=$(CUDA_ARCH)
	PATH="$(CUDA_HOME)/bin:$$PATH" \
		cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-dcu:
	cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_GROTH16_ACCELERATOR=DCU \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)" $(DCU_ARCH_FLAG)
	cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-test-cpu: groth16-cpu
	$(GROTH16_BIN_DIR)/groth16-gen $(PROFILE_FLAG) matmul $(GROTH16_CPU_MATRIX_SIZE)
	$(GROTH16_BIN_DIR)/groth16-setup $(PROFILE_FLAG) --backend cpu matmul_$(GROTH16_CPU_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-prove $(PROFILE_FLAG) --backend cpu matmul_$(GROTH16_CPU_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-verify $(PROFILE_FLAG) matmul_$(GROTH16_CPU_MATRIX_SIZE).bin

groth16-test-cuda: groth16-cuda
	$(GROTH16_BIN_DIR)/groth16-gen $(PROFILE_FLAG) matmul $(GROTH16_CUDA_MATRIX_SIZE)
	$(GROTH16_BIN_DIR)/groth16-setup $(PROFILE_FLAG) --backend cuda matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-prove $(PROFILE_FLAG) --backend cuda matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-verify $(PROFILE_FLAG) matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin

groth16-test-dcu: groth16-dcu
	$(GROTH16_BIN_DIR)/groth16-gen $(PROFILE_FLAG) matmul $(GROTH16_DCU_MATRIX_SIZE)
	$(GROTH16_BIN_DIR)/groth16-setup $(PROFILE_FLAG) --backend dcu matmul_$(GROTH16_DCU_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-prove $(PROFILE_FLAG) --backend dcu matmul_$(GROTH16_DCU_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-verify $(PROFILE_FLAG) matmul_$(GROTH16_DCU_MATRIX_SIZE).bin

groth16-clean:
	cmake -E rm -rf \
		"$(GROTH16_BUILD_DIR)" \
		"$(BUILD_DIR)/groth16-cpu" \
		"$(BUILD_DIR)/groth16-cuda" \
		"$(BUILD_DIR)/groth16-dcu"

plonk-cpu:
	cargo build --release --manifest-path protocols/plonk/implementations/cpu/Cargo.toml

plonk-test:
	cargo run --release --manifest-path protocols/plonk/implementations/cpu/Cargo.toml \
		--example simple_circuit

plonk-cuda:
	PATH="$(CUDA_HOME)/bin:$$PATH" \
	LD_LIBRARY_PATH="$(CUDA_HOME)/lib64:$(CUDA_HOME)/targets/x86_64-linux/lib:$${LD_LIBRARY_PATH:-}" \
	cargo build --release \
		--manifest-path protocols/plonk/implementations/cuda/Cargo.toml

plonk-test-cuda: plonk-cuda
	PATH="$(CUDA_HOME)/bin:$$PATH" \
	LD_LIBRARY_PATH="$(CUDA_HOME)/lib64:$(CUDA_HOME)/targets/x86_64-linux/lib:$${LD_LIBRARY_PATH:-}" \
	cargo run --release --manifest-path protocols/plonk/implementations/cuda/Cargo.toml \
		--example simple_circuit

stark:
	RAYON_NUM_THREADS=$(THREADS) cargo build --release \
		--manifest-path protocols/stark/winterfell/Cargo.toml \
		-p examples --features concurrent

stark-matmul:
	RAYON_NUM_THREADS=$(THREADS) cargo run --release \
		--manifest-path protocols/stark/winterfell/Cargo.toml \
		-p examples --features concurrent -- matrix-mul -n $(MATRIX_SIZE) $(PROFILE_FLAG)

clean: groth16-clean
	cargo clean --manifest-path protocols/plonk/implementations/cpu/Cargo.toml
	cargo clean --manifest-path protocols/plonk/implementations/cuda/Cargo.toml
	cargo clean --manifest-path protocols/stark/winterfell/Cargo.toml
