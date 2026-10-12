.DEFAULT_GOAL := help

BUILD_DIR ?= build
JOBS ?= 4
THREADS ?= 4
MATRIX_SIZE ?= 8

.PHONY: help check groth16-cuda groth16-dcu plonk-cpu plonk-cuda stark stark-matmul clean

help:
	@echo "NKR-ZKP targets:"
	@echo "  check          validate repository layout"
	@echo "  groth16-cuda   configure and build the NVIDIA CUDA backend"
	@echo "  groth16-dcu    configure and build the HIP/DCU backend"
	@echo "  plonk-cpu      build the CPU PLONK snapshot"
	@echo "  plonk-cuda     build the experimental CUDA PLONK snapshot"
	@echo "  stark          build Winterfell with concurrent CPU support"
	@echo "  stark-matmul   run the Winterfell matrix multiplication example"

check:
	@./scripts/check-layout.sh

groth16-cuda:
	cmake -S . -B $(BUILD_DIR)/groth16-cuda \
		-DNKR_ZKP_GROTH16_BACKEND=CUDA \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)"
	cmake --build $(BUILD_DIR)/groth16-cuda -j$(JOBS)

groth16-dcu:
	cmake -S . -B $(BUILD_DIR)/groth16-dcu \
		-DNKR_ZKP_GROTH16_BACKEND=DCU \
		-DNKR_ZKP_GROTH16_DEPS_DIR="$(GROTH16_DEPS_DIR)"
	cmake --build $(BUILD_DIR)/groth16-dcu -j$(JOBS)

plonk-cpu:
	cargo build --release --manifest-path protocols/plonk/implementations/cpu/Cargo.toml

plonk-cuda:
	cargo build --release --manifest-path protocols/plonk/implementations/cuda/Cargo.toml

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
