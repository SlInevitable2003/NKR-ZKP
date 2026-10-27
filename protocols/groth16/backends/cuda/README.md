# Groth16 CUDA 适配层

本目录包含 Groth16 专属的 CUDA 流程编排、QAP 和稀疏矩阵运算。通用的 BN254 域、曲线、NTT、MSM 及运行时代码位于 `operators/cuda`，并通过 `nkr_operators_cuda` 目标链接。

在仓库根目录执行 `make groth16-test-cuda` 即可构建并运行测试。
