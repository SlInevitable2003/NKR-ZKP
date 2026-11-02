# Groth16 HIP 适配层

本目录包含 Groth16 专属的 HIP Setup/Prove 适配层、QAP 和 SpMV 实现。通用的 BN254 域、曲线、NTT、MSM 及运行时代码位于 `operators/hip`，并通过 `nkr_operators_hip` 目标链接。

该后端面向无进位链且不可靠支持 warp shuffle 的 DCU，避免使用 cooperative groups。在仓库根目录执行 `make groth16-test-hip HIP_ARCH=<gfx-arch>` 即可构建并运行端到端测试。
