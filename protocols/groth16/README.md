# Groth16

Groth16 采用项目统一的协议目录结构：

- `app/`：与平台无关的 `gen/setup/prove/verify` 程序；
- `common/`：协议类型、I/O、校验、验证、性能分析及后端选择；
- `backends/cpu`：基于 libff/libfqfft 的参考实现；
- `backends/cuda` 和 `backends/hip`：包含 QAP、SpMV 及其他 Groth16 专属数据流的协议适配层。

BN254 域、曲线、NTT、MSM 及设备运行时代码通过顶层 `operators/cuda` 和 `operators/hip` 目标共享，不在本协议目录中重复保存。

## 统一程序与后端选择

所有后端共用 `groth16-gen`、`groth16-setup`、`groth16-prove` 和 `groth16-verify` 四个程序，不为 CPU、CUDA 或 HIP 生成不同名称的可执行文件。

`groth16-cpu`、`groth16-cuda` 和 `groth16-hip` 是构建配置，不是程序名。它们决定统一程序中可供 `--backend` 选择的实现：CPU 配置提供 `cpu`，CUDA 配置提供 `cpu` 与 `cuda`，HIP 配置提供 `cpu` 与 `hip`。`gen` 和 `verify` 与后端无关，不接收 `--backend`。

CPU、CUDA 和 HIP 从构建到验证的完整命令统一维护在仓库根目录的 [Groth16 构建与运行](../../README.md#groth16-构建与运行) 一节，避免两处文档重复后产生差异。

## CPU 并行策略

CPU Setup 为 G1/G2 分别预计算固定基窗口表，并通过公共 CPU FixMSM 算子并行生成 A、B、L、H 和 IC 查询。并行度由 `OMP_NUM_THREADS` 控制。CPU Prove 用于跨实现性能比较，因此 SpMV、NTT 和变基 MSM 均保持单线程；项目不会启用 libff/libfqfft 的全局 `MULTICORE` 宏。
