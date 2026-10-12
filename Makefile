.DEFAULT_GOAL := help

BUILD_DIR ?= build
JOBS ?= 2
THREADS ?= 4
MATRIX_SIZE ?= 4
CUDA_ARCH ?= 120
CUDA_HOME ?= /usr/local/cuda
GROTH16_CPU_MATRIX_SIZE ?= 4
GROTH16_CUDA_MATRIX_SIZE ?= 12
GROTH16_DEPS_DIR ?= $(CURDIR)/protocols/groth16/third_party/depends

.PHONY: help check groth16-cpu groth16-cuda groth16-dcu groth16-test-cpu groth16-test-cuda plonk-cpu plonk-test plonk-cuda plonk-test-cuda stark stark-matmul clean

help:
	@echo "NKR-ZKP targets:"
	@echo "  check          validate repository layout"
	@echo "  groth16-cpu    configure and build the CPU backend"
	@echo "  groth16-cuda   configure and build the NVIDIA CUDA backend"
	@echo "  groth16-dcu    configure and build the HIP/DCU backend"
	@echo "  groth16-test-cpu   run a small CPU setup/prove/verify flow"
	@echo "  groth16-test-cuda  run a CUDA setup/prove/verify flow"
	@echo "  plonk-cpu      build the CPU PLONK snapshot"
	@echo "  plonk-test     run a small PLONK CPU proof"
	@echo "  plonk-cuda     build the experimental CUDA PLONK snapshot"
	@echo "  plonk-test-cuda  run the small PLONK CUDA proof example"
	@echo "  stark          build Winterfell with concurrent CPU support"
	@echo "  stark-matmul   run the Winterfell matrix multiplication example"

check:
	@./scripts/check-layout.sh

groth16-cpu:
	cmake -S . -B $(BUILD_DIR)/groth16-cpu \
		-DNKR_ZKP_GROTH16_BACKEND=CPU \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)"
	cmake --build $(BUILD_DIR)/groth16-cpu --parallel $(JOBS)

groth16-cuda:
	PATH="$(CUDA_HOME)/bin:$$PATH" CUDACXX="$(CUDA_HOME)/bin/nvcc" cmake -S . -B $(BUILD_DIR)/groth16-cuda \
		-DNKR_ZKP_GROTH16_BACKEND=CUDA \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)" \
		-DCMAKE_CUDA_ARCHITECTURES=$(CUDA_ARCH)
	PATH="$(CUDA_HOME)/bin:$$PATH" \
		cmake --build $(BUILD_DIR)/groth16-cuda --parallel $(JOBS)

groth16-dcu:
	cmake -S . -B $(BUILD_DIR)/groth16-dcu \
		-DNKR_ZKP_GROTH16_BACKEND=DCU \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)"
	cmake --build $(BUILD_DIR)/groth16-dcu -j$(JOBS)

groth16-test-cpu: groth16-cpu
	$(BUILD_DIR)/groth16-cpu/protocols/groth16/groth16-gen matmul $(GROTH16_CPU_MATRIX_SIZE)
	$(BUILD_DIR)/groth16-cpu/protocols/groth16/groth16-setup --backend cpu matmul_$(GROTH16_CPU_MATRIX_SIZE).bin
	$(BUILD_DIR)/groth16-cpu/protocols/groth16/groth16-prove --backend cpu matmul_$(GROTH16_CPU_MATRIX_SIZE).bin
	$(BUILD_DIR)/groth16-cpu/protocols/groth16/groth16-verify matmul_$(GROTH16_CPU_MATRIX_SIZE).bin

groth16-test-cuda: groth16-cuda
	$(BUILD_DIR)/groth16-cuda/protocols/groth16/groth16-gen matmul $(GROTH16_CUDA_MATRIX_SIZE)
	$(BUILD_DIR)/groth16-cuda/protocols/groth16/groth16-setup --backend cuda matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin
	$(BUILD_DIR)/groth16-cuda/protocols/groth16/groth16-prove --backend cuda matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin
	$(BUILD_DIR)/groth16-cuda/protocols/groth16/groth16-verify matmul_$(GROTH16_CUDA_MATRIX_SIZE).bin

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
		-p examples --features concurrent -- matrix-mul -n $(MATRIX_SIZE)

clean:
	cargo clean --manifest-path protocols/plonk/implementations/cpu/Cargo.toml
	cargo clean --manifest-path protocols/plonk/implementations/cuda/Cargo.toml
	cargo clean --manifest-path protocols/stark/winterfell/Cargo.toml
