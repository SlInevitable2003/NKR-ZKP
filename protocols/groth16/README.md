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

## 支持的电路

`groth16-gen` 当前支持矩阵乘法、Fibonacci、Poseidon hash 和可验证 SQL 聚合：

```bash
groth16-gen matmul <matrix-size>
groth16-gen fibonacci <steps>
groth16-gen poseidon [count]
groth16-gen sql-aggregate <rows>
```

矩阵乘法、Poseidon 和 SQL 聚合支持 `--density full|hybrid|sparse`（默认 `full`）：
`full` 使用 100% 随机非零输入，`hybrid` 保留 50%，`sparse` 保留 10%，其余置零并
随机打散。`matmul` 对 A、B 各 `N` 个元素独立采样；`poseidon` 对每个哈希的左右
输入（共 `2 × count` 个）独立采样；`sql-aggregate` 只对每行 amount 采样，
category 保持 0..15 随机。非零个数为 `ceil(N × 密度)`，非 `full` 档位的产物
文件名追加 `_hybrid` / `_sparse`。完整命令见根目录 README 的「Groth16 构建与
运行」一节；该参数仅对 `matmul`、`poseidon` 和 `sql-aggregate` 生效，`fibonacci`
传入非 `full` 值会报错。

Fibonacci 从两个公开种子 `F0, F1` 开始，递推 `Fi = F(i-2) + F(i-1)`，
将 `F_steps` 作为第三个公开输入，所有中间项为私有见证。`steps` 必须不小于 2，
见证由同一递推计算，每步加法和最终结果均由 R1CS 约束绑定；该电路不支持
`--density`。

Poseidon 使用 BN254 标量域上的 `x^5`、`t=3`、`R_F=8`、`R_P=57`
参数，二输入哈希定义为 `Poseidon([0, left, right])[0]`。每个实例将左右
原像作为私有见证，将摘要作为公开输入；`count` 默认为 1，可用于生成多个相互
独立的哈希约束。参数与 CUDA 目录中的参考 Sage 文件一致。

SQL 示例实现以下固定查询，其中私有表由公开的 Poseidon Merkle Root 绑定，
查询条件、COUNT 和 SUM 均为公开输入：

```sql
SELECT COUNT(*), SUM(amount)
FROM transactions
WHERE category = :target_category;
```

`category` 和 `amount` 都约束为 `UINT32`，不支持 NULL、字符串、JOIN、排序或
任意 SQL 解析。`rows` 必须是不小于 2 的二次幂。1024 行会产生 969324 条
原始约束，R1CS 行列均填充为 `2^20`。`--density` 只把部分 amount 置零，
category 保持随机；非 `full` 档位的产物文件名追加 `_hybrid` / `_sparse`。

约束测试和 CPU 端到端回归可通过以下命令运行：

```bash
make groth16-test-poseidon PROFILE=1 GROTH16_POSEIDON_COUNT=1
make groth16-test-sql-aggregate PROFILE=1 GROTH16_SQL_ROWS=4
```

## Prove 封装与计时

Groth16 与 PLONK 采用一致的 `prepare/prove` 生命周期。CPU 的 prepare 为空；CUDA 与 HIP 均在 prepare 中建立设备布局、生成 NTT 幂表并上传 PK，在 prove 中只上传 witness 并执行在线证明数据流。`groth16.prove.program_total` 表示命令行程序的端到端时间，`groth16.prove.prepare_backend` 表示可预处理阶段，`groth16.prove.total` 则只包含 raw proof 生成和最终盲化。

`groth16-prove` 支持 `--output <proof-file>` 覆盖 proof 输出路径；`groth16-verify` 支持 `--proof <proof-file>` 从指定路径读取 proof。两者只改 app 入口的路径解析，不影响协议数据。

## CPU 并行策略

CPU Setup 为 G1/G2 分别预计算固定基窗口表，并通过公共 CPU FixMSM 算子并行生成 A、B、L、H 和 IC 查询。并行度由 `OMP_NUM_THREADS` 控制。CPU Prove 用于跨实现性能比较，因此 SpMV、NTT 和变基 MSM 均保持单线程；项目不会启用 libff/libfqfft 的全局 `MULTICORE` 宏。
