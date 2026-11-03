# zk-STARK / Winterfell

`winterfell/` 是独立的 Rust workspace，包含完整的 STARK Prover、Verifier、
AIR、FRI、有限域/FFT、Merkle 承诺和示例程序。导入来源及基准提交见
[`docs/provenance.md`](../../docs/provenance.md)。它不参与仓库根目录的 CMake
构建，已同样支持在 CPU、NVIDIA GPU 和 DCU 平台环境中构建与运行；启用
`concurrent` feature 后由 Rayon 并行调度证明任务。

STARK 是透明证明系统，不需要 Groth16/PLONK 风格的可信设置。示例程序在单次
运行中生成执行轨迹和证明，序列化并重新解析证明，随后完成验证。默认输出包括
证明生成时间、证明大小、安全级别、证明哈希和验证时间。

## 构建与运行

需要 Rust 1.87 和 Cargo。仓库根目录的便捷命令为：

```bash
make stark THREADS=8
make stark-matmul THREADS=8 MATRIX_SIZE=8 PROFILE=1
```

其中 `THREADS` 设置 `RAYON_NUM_THREADS`，`MATRIX_SIZE` 必须是不小于 2 的
2 的幂，`PROFILE=1` 输出各阶段的详细耗时。release 可执行文件位于：

```text
protocols/stark/winterfell/target/release/winterfell
```

直接使用示例程序时，可先查看完整命令列表：

```bash
protocols/stark/winterfell/target/release/winterfell --help
```

除 `matrix-mul` 外，workspace 还包含 Fibonacci、乘法 Fibonacci、VDF、
Rescue、随机化 AIR、Merkle membership 和 Lamport+ signature 等示例。

## 安全边界

Winterfell 上游将其定位为未经审计的研究项目，不应直接用于生产环境。当前实现
能够生成简洁的计算完整性证明，但尚不提供完美零知识；如果应用要求隐藏秘密
输入或见证，需要先补充并审计相应的零知识机制，不能仅因协议通常被称为
“zk-STARK”就假定当前实现已经满足该性质。
