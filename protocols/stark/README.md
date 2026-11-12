# zk-STARK / Winterfell

`winterfell/` 是独立的 Rust workspace，包含完整的 STARK Prover、Verifier、
AIR、FRI、有限域/FFT、Merkle 承诺和示例程序。导入来源及基准提交见
[`docs/provenance.md`](../../docs/provenance.md)。它不参与仓库根目录的 CMake
构建，目前只有 CPU 实现；在 NVIDIA GPU 或 DCU 机器上运行也仍然使用 CPU。
启用 `concurrent` feature 后由 Rayon 并行调度证明任务。

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

`matrix-mul`、`poseidon` 和 `sql` 支持 `--density full|hybrid|sparse`（默认 `full`）：
`full` 使用 100% 随机非零输入，`hybrid` 保留 50%，`sparse` 保留 10%，其余置零
并随机打散。`matrix-mul` 对 A、B 各 `n²` 个元素独立采样，`poseidon` 对
`2 × 哈希数` 个输入独立采样，`sql` 只对 amount 列采样；`make stark-matmul`
可通过 `DENSITY=sparse` 传入。其他示例传入非 `full` 值会报错。

Poseidon 示例：

```bash
protocols/stark/winterfell/target/release/winterfell \
  poseidon -n 2 --density sparse --profile
```

`-n` 为哈希个数，必须为 2 的幂。STARK 版本使用 f128 域上的 Poseidon 参数
（`x^5`、`t=3`、`R_F=8`、`R_P=57`，round constants 由 Grain LFSR 生成，MDS 为
Cauchy 矩阵），摘要值与 BN254 的 Groth16/PLONK 电路不同。

SQL 聚合示例：

```bash
protocols/stark/winterfell/target/release/winterfell \
  sql -n 4 --density sparse --profile
```

`-n` 为表行数（2 的幂且不小于 2）。私有表按行顺序用 Poseidon 哈希链承诺
（`chain_i = H(chain_{i-1}, H(category_i, amount_i))`），公开输入为 target、
COUNT、SUM 和链摘要。STARK 版本按 f128 域元素聚合，不额外做 UINT32 分解；
`--density` 只作用于 amount 列。

除 `matrix-mul` 外，workspace 还包含 Fibonacci、Poseidon、SQL 聚合、乘法 Fibonacci、VDF、
Rescue、随机化 AIR、Merkle membership 和 Lamport+ signature 等示例。

## 安全边界

Winterfell 上游将其定位为未经审计的研究项目，不应直接用于生产环境。当前实现
能够生成简洁的计算完整性证明，但尚不提供完美零知识；如果应用要求隐藏秘密
输入或见证，需要先补充并审计相应的零知识机制，不能仅因协议通常被称为
“zk-STARK”就假定当前实现已经满足该性质。
