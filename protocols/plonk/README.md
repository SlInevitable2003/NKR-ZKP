# PLONK

当前保留两个来源快照：

- `implementations/cpu`：纯 CPU 基线；
- `implementations/cuda`：修改过协议核心、`ark-poly-commit` 和 MSM/FFT 路径的 CUDA 实验版本。

它们暂不组成同一个 Cargo workspace，避免不同锁文件、工具链和同名 package 相互污染。待 CPU 测试向量固定后，将以 CPU 版本作为协议公共层，把 CUDA 目录中的加速部分迁移为 feature-gated backend。

