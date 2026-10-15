# 架构与演进约束

## 分层原则

框架分为三层：

1. **协议层**：约束系统、证明/验证流程、序列化格式和应用电路；
2. **算子层**：MSM、NTT/FFT、哈希、矩阵运算等可加速原语；
3. **后端层**：CPU、CUDA、HIP/DCU 的内存、流、kernel launch 和设备实现。

协议层不得直接包含 `cuda_runtime.h`、`hip_runtime.h` 或设备专属类型。算子接口使用宿主侧普通类型和显式上下文；设备缓冲区只在后端内部出现。CPU 后端同时作为正确性基线和无加速器回退实现。

## 当前状态

Groth16 的 `app/` 现在提供同一套 `gen/setup/prove/verify` 协议入口。gen 和 verify 只链接公共协议层；setup 和 prove 通过 `Backend` 接口在当前构建已包含的 CPU、CUDA 或 DCU 实现之间选择。CPU 始终可用，CUDA/DCU 作为依赖不同工具链的可选加速器在构建时选择，所有配置共用 `build/groth16`。验证、盲化、文件格式、协议类型和 R1CS 数据结构均位于 `common/`。CUDA 与 DCU 的运行时、设备缓冲区和 kernel 只保留在各自后端中。DCU 导入版本目前仅加速 prove，因此 DCU setup 会显式委托 CPU 基线。现存 `.cuh`/`.hip` 大部分仍同时含算法和运行时调用，后续应按算子逐个抽取，不能只做全局文本替换。

PLONK 的两个目录来自没有 Git 历史的独立实验快照，而且 GPU 版本修改了协议核心和 `ark-poly-commit`。为避免错误合并，当前保留 CPU 与 CUDA 两个可独立审计的实现。下一阶段应先定义 `PolynomialCommitmentBackend` 和 `MsmBackend`，然后以 CPU 版本为公共协议层，把 CUDA fork 变成可选后端。

Winterfell 当前仍是 CPU 实现。Rayon 的固定线程数由 `RAYON_NUM_THREADS` 控制，可作为模拟 GPU/DCU 并行度的实验变量；矩阵乘法应用位于 `examples/src/matrixmul`。

## 推荐迁移顺序

1. 为每个协议建立 CPU golden test，并固定证明/验证测试向量；
2. 抽取统一的后端能力与错误类型，但保持协议专属的域和曲线类型；
3. 优先迁移无状态算子（哈希、域运算），再迁移 NTT/FFT 和 MSM；
4. 对 CUDA 与 HIP/DCU 使用同一组算子契约和测试数据，不强求共享 kernel 源码；
5. 最后统一 CLI/benchmark 元数据，避免过早统一三个协议的内部证明 API。

## Git 规则

- 主分支只接收可复现的源码、锁文件、文档和小型测试向量；
- `build/`、`target/`、kernel 二进制与性能输出不入库；
- 外部代码升级时同步更新 `docs/provenance.md`；
- 每次平台移植独立提交，禁止在同一提交中同时做语义重构和机械 hipify；
- 原始本地目录保留不动，直到本仓库的对应快照完成验证。
