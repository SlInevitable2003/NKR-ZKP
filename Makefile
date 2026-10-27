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
HIP_ARCH ?=
CUDA_HOME ?= /usr/local/cuda
GROTH16_CPU_MATRIX_SIZE ?= 4
GROTH16_CUDA_MATRIX_SIZE ?= 12
GROTH16_HIP_MATRIX_SIZE ?= 12
LIGHTZK_DEPS_DIR ?= $(CURDIR)/third_party/lightzk/depends
PROFILE_FLAG = $(if $(filter 1 true yes on,$(PROFILE)),--profile,)
HIP_ARCH_FLAG = $(if $(strip $(HIP_ARCH)),-DCMAKE_HIP_ARCHITECTURES=$(HIP_ARCH),)

.PHONY: help check groth16-cpu groth16-cuda groth16-hip groth16-dcu groth16-test-cpu groth16-test-cuda groth16-test-hip groth16-test-dcu groth16-clean plonk-scaffold stark stark-matmul clean

help:
	@echo "NKR-ZKP targets:"
	@echo "  check          validate repository layout"
	@echo "  groth16-cpu    configure and build the CPU backend"
	@echo "  groth16-cuda   configure and build the NVIDIA CUDA backend"
	@echo "  groth16-hip    configure and build the HIP backend"
	@echo "  groth16-test-cpu   run a small CPU setup/prove/verify flow"
	@echo "  groth16-test-cuda  run a CUDA setup/prove/verify flow"
	@echo "  groth16-test-hip   run a HIP setup/prove/verify flow"
	@echo "  groth16-clean  remove all Groth16 CMake build trees"
	@echo "  plonk-scaffold build the PLONK interfaces and placeholder apps"
	@echo "  stark          build Winterfell with concurrent CPU support"
	@echo "  stark-matmul   run the Winterfell matrix multiplication example"

check:
	@./scripts/check-layout.sh

groth16-cpu:
	cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_ACCELERATOR=NONE \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_LIGHTZK_DEPS_DIR="$(LIGHTZK_DEPS_DIR)"
	cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-cuda:
	PATH="$(CUDA_HOME)/bin:$$PATH" CUDACXX="$(CUDA_HOME)/bin/nvcc" cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_ACCELERATOR=CUDA \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_LIGHTZK_DEPS_DIR="$(LIGHTZK_DEPS_DIR)" \
		-DCMAKE_CUDA_ARCHITECTURES=$(CUDA_ARCH)
	PATH="$(CUDA_HOME)/bin:$$PATH" \
		cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-hip:
	cmake -S . -B $(GROTH16_BUILD_DIR) \
		-DNKR_ZKP_ACCELERATOR=HIP \
		-DNKR_ZKP_GROTH16_BIN_DIR="$(abspath $(GROTH16_BIN_DIR))" \
		-DNKR_ZKP_GROTH16_DATA_DIR="$(abspath $(GROTH16_DATA_DIR))" \
		-DNKR_ZKP_LIGHTZK_DEPS_DIR="$(LIGHTZK_DEPS_DIR)" $(HIP_ARCH_FLAG)
	cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS)

groth16-dcu: groth16-hip

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

groth16-test-hip: groth16-hip
	$(GROTH16_BIN_DIR)/groth16-gen $(PROFILE_FLAG) matmul $(GROTH16_HIP_MATRIX_SIZE)
	$(GROTH16_BIN_DIR)/groth16-setup $(PROFILE_FLAG) --backend hip matmul_$(GROTH16_HIP_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-prove $(PROFILE_FLAG) --backend hip matmul_$(GROTH16_HIP_MATRIX_SIZE).bin
	$(GROTH16_BIN_DIR)/groth16-verify $(PROFILE_FLAG) matmul_$(GROTH16_HIP_MATRIX_SIZE).bin

groth16-test-dcu: groth16-test-hip

groth16-clean:
	cmake -E rm -rf \
		"$(GROTH16_BUILD_DIR)" \
		"$(BUILD_DIR)/groth16-cpu" \
		"$(BUILD_DIR)/groth16-cuda" \
		"$(BUILD_DIR)/groth16-hip" \
		"$(BUILD_DIR)/groth16-dcu"

plonk-scaffold: groth16-cpu
	cmake --build $(GROTH16_BUILD_DIR) --parallel $(JOBS) \
		--target plonk-gen plonk-setup plonk-prove plonk-verify

stark:
	RAYON_NUM_THREADS=$(THREADS) cargo build --release \
		--manifest-path protocols/stark/winterfell/Cargo.toml \
		-p examples --features concurrent

stark-matmul:
	RAYON_NUM_THREADS=$(THREADS) cargo run --release \
		--manifest-path protocols/stark/winterfell/Cargo.toml \
		-p examples --features concurrent -- matrix-mul -n $(MATRIX_SIZE) $(PROFILE_FLAG)

clean: groth16-clean
	cargo clean --manifest-path protocols/stark/winterfell/Cargo.toml
