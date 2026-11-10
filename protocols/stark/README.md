# zk-STARK / Winterfell

`winterfell/` 是独立的 Rust workspace，包含完整的 STARK Prover、Verifier、
AIR、FRI、有限域/FFT、Merkle 承诺和示例程序。导入来源及基准提交见
[`docs/provenance.md`](../../docs/provenance.md)。它不参与仓库根目录的 CMake
构建。目前只有 CPU 实现，没有 CUDA/HIP 加速后端；在 NVIDIA GPU 或 DCU
机器上运行也仍然使用 CPU。启用 `concurrent` feature 后由 Rayon 并行调度证明任务。

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

## 输入数据模式

`matrix-mul`（别名 `matmul`）和 `fib`（别名 `fibonacci`）支持全局参数
`--data-mode dense|sparse|hybrid`，可放在子命令前后，默认 `dense`。
这里的 `fib` 是双列执行轨迹的 Fibonacci 示例；其他 Fibonacci 变体及其他
示例不支持 sparse/hybrid。

- `dense`：100% 输入为随机非零域元素。
- `sparse`：50% 输入为随机非零域元素，非零数量为 `ceil(N / 2)`，其余为零。
- `hybrid`：10% 输入为随机非零域元素，非零数量为 `ceil(N / 10)`，其余为零。

sparse/hybrid 的零值位置全局随机打散，不保留 dense 前缀，与公共
`input_data.hpp` 一致。比例只作用于原始输入，不修改中间轨迹或结果。
Fibonacci 只有两个初始值，所以 sparse 和 hybrid 均为一个随机非零值、一个零值。

```bash
protocols/stark/winterfell/target/release/winterfell \
  matrix-mul -n 8 --data-mode sparse --profile
protocols/stark/winterfell/target/release/winterfell \
  fib -n 32 --data-mode hybrid --profile
```

## 安全边界

Winterfell 上游将其定位为未经审计的研究项目，不应直接用于生产环境。当前实现
能够生成简洁的计算完整性证明，但尚不提供完美零知识；如果应用要求隐藏秘密
输入或见证，需要先补充并审计相应的零知识机制，不能仅因协议通常被称为
“zk-STARK”就假定当前实现已经满足该性质。
