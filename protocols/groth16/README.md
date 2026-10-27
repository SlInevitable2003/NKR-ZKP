# Groth16

Groth16 采用项目统一的协议目录结构：

- `app/`：与平台无关的 `gen/setup/prove/verify` 程序；
- `common/`：协议类型、I/O、校验、验证、性能分析及后端选择；
- `backends/cpu`：基于 libff/libfqfft 的参考实现；
- `backends/cuda` 和 `backends/hip`：包含 QAP、SpMV 及其他 Groth16 专属数据流的协议适配层。

BN254 域、曲线、NTT、MSM 及设备运行时代码通过顶层 `operators/cuda` 和 `operators/hip` 目标共享，不在本协议目录中重复保存。

## 后端构建

在仓库根目录执行。仅构建时使用：

```bash
make groth16-cpu
make groth16-cuda CUDA_ARCH=80
make groth16-hip HIP_ARCH=<gfx-arch>
```

构建并执行小规模端到端测试时使用：

```bash
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4
make groth16-test-cuda CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=12
make groth16-test-hip HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=12
```

- CPU 构建只提供 `cpu` 后端。
- CUDA 构建同时提供 `cpu` 和 `cuda` 后端；`CUDA_ARCH` 是目标 NVIDIA GPU 的计算能力，`CUDA_HOME` 默认值为 `/usr/local/cuda`。
- HIP 构建同时提供 `cpu` 和 `hip` 后端；构建前需要让 CMake 能够找到 HIP/ROCm 工具链，并将 `HIP_ARCH` 设置为目标 GPU 的 offload target。
- `groth16-dcu` 和 `groth16-test-dcu` 分别是 HIP 构建和测试目标的兼容性别名。

一个 CMake 构建目录最多包含一种加速器后端。并行保留多种配置时应使用不同目录：

```bash
make groth16-cpu  GROTH16_BUILD_DIR=build/groth16-cpu
make groth16-cuda GROTH16_BUILD_DIR=build/groth16-cuda CUDA_ARCH=80
make groth16-hip  GROTH16_BUILD_DIR=build/groth16-hip HIP_ARCH=<gfx-arch>
```

`setup` 和 `prove` 通过 `--backend cpu|cuda|hip` 选择实际实现；`gen` 和 `verify` 仅链接协议公共层，不需要后端参数。当前 HIP 后端的 `setup` 委托给 CPU 参考实现，`prove` 使用 HIP 加速。

完整的依赖要求、手动运行示例和 CMake 选项见仓库根目录的 [README](../../README.md)。
