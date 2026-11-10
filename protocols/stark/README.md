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
示例不支持 sparse/hybrid，显式指定时会报错。

- `dense`：所有输入均为随机非零域元素。
- `sparse`：对于 N 个输入，随机分布恰好 `floor(N / 2)` 个零，其余为随机非零值。
- `hybrid`：前 `ceil(N / 2)` 个输入为随机非零值；后 `floor(N / 2)` 个输入
  采用 sparse 模式，整个输入恰好有 `floor(floor(N / 2) / 2)` 个零。

与 Groth16/PLONK 相同，模式只作用于输入，而不是中间轨迹或输出。
矩阵输入按 `A[0], B[0], A[1], B[1], ...` 的顺序应用模式；两个矩阵内部
均为行主序。Fibonacci 的输入只有两个初始值，因此 sparse 恰好一个零，
hybrid 因取整没有零。Fibonacci AIR 同时绑定两个初始值与最终结果。
矩阵边长须为不小于 2 的 2 的幂，Fibonacci 长度须为不小于 16 的 2 的幂。

```bash
protocols/stark/winterfell/target/release/winterfell \
  matrix-mul -n 8 --data-mode sparse --profile
protocols/stark/winterfell/target/release/winterfell \
  fib -n 32 --data-mode hybrid --profile
```

## Slurm 完整测试

在仓库根目录提交：

```bash
sbatch scripts/test-stark-data-modes.sh
```

脚本默认使用已安装的 `stable` 工具链，可用 `STARK_RUST_TOOLCHAIN` 覆盖，
不依赖 workspace 固定的 `1.87` 工具链。

脚本只编译一次，并对 matmul/fibonacci 的 dense/sparse/hybrid 各执行一次
完整的输入生成、执行轨迹构造、证明生成、序列化往返和验证，共六次运行，
全部带 `--profile`。STARK 不需要 setup 阶段。验证失败以非零状态退出，
作业不会打印对应的 PASS。

默认矩阵边长 64、Fibonacci 长度 32、Rayon 线程数等于 Slurm 分配的 CPU 数。
可通过环境变量覆盖：

```bash
sbatch --cpus-per-task=8 \
  --export=ALL,STARK_RUST_TOOLCHAIN=stable,STARK_MATRIX_SIZE=8,STARK_FIBONACCI_LENGTH=64 \
  scripts/test-stark-data-modes.sh
```

`STARK_MODES_BUILD_DIR` 可指定 Cargo 产物目录，默认
`/tmp/stark-data-modes-${SLURM_JOB_ID}`。脚本使用 CPU/Rayon，不提供 CUDA/HIP
切换；`module load cuda` 本身不会让 Winterfell 使用 GPU。

## 安全边界

Winterfell 上游将其定位为未经审计的研究项目，不应直接用于生产环境。当前实现
能够生成简洁的计算完整性证明，但尚不提供完美零知识；如果应用要求隐藏秘密
输入或见证，需要先补充并审计相应的零知识机制，不能仅因协议通常被称为
“zk-STARK”就假定当前实现已经满足该性质。
