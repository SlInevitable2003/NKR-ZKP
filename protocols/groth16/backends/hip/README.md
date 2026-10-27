# Groth16 HIP 适配层

本目录包含 Groth16 专属的 HIP 证明适配层和 SpMV 实现。通用的 BN254 域、曲线、NTT、MSM 及运行时代码位于 `operators/hip`，并通过 `nkr_operators_hip` 目标链接。

当前导入的 HIP 后端只加速证明过程；`setup` 暂时委托给 CPU 参考后端。在仓库根目录执行 `make groth16-test-hip HIP_ARCH=<gfx-arch>` 即可构建并运行测试。
