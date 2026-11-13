# NKR-ZKP 测试脚本

| 脚本 | 测试 |
| --- | --- |
| `functional.sh` | 功能测试: Groth16/PLONK/STARK 的 Gen/Setup/Prove/Verify |
| `single-card.sh` | Groth16 单卡测试(CPU/GPU/DCU) |
| `multi-card.sh` | Groth16 多卡测试(GPU/DCU) |
| `distributed.sh` | Groth16 多机多卡测试(GPU/DCU, Slurm) |

## 功能测试

```bash
./scripts/functional.sh gpu --run-id func-20261113   # GPU 机器
./scripts/functional.sh dcu --run-id func-20261113   # DCU 机器
./scripts/functional.sh cpu --run-id func-20261113   # 纯 CPU
```

覆盖 `matmul`、`fibonacci`、`poseidon`、`sql`。Groth16/PLONK 在 CPU/GPU/DCU
上执行；STARK 目前只有 CPU 实现，`GPU-sim`/`DCU-sim` 是 CPU 线程标签。

## 单卡测试

```bash
./scripts/single-card.sh gpu --run-id p1-20261113
./scripts/single-card.sh dcu --run-id p1-20261113
```

Groth16，`k=20..26`，全部电路/密度，CPU 基准 + 单 GPU/DCU。
`concurrency: auto` 会先在本机标定并发度，再正式跑；没有单独的标定命令。

## 多卡测试

```bash
./scripts/multi-card.sh gpu --run-id p2-20261113
./scripts/multi-card.sh dcu --run-id p2-20261113
```

Groth16，GPU 4 卡 / DCU 8 卡，每张卡跑相同规模的独立证明。

## 分布式测试

多机分发由 Slurm 负责；每个节点运行：

```bash
# GPU 集群
./scripts/distributed.sh gpu --node "$SLURMD_NODENAME" --run-id p3-20261113

# DCU 集群
./scripts/distributed.sh dcu --node "$SLURMD_NODENAME" --run-id p3-20261113
```

每个节点只做本机的事：本机 Setup、本机所有卡跑 16 个 `2^26` 证明，
结果写入共享目录：

```text
results/<run_id>/nodes/<node>/
```

所有节点完成后，在任意节点汇总：

```bash
./scripts/distributed.sh --aggregate --run-id p3-20261113
```

汇总计算等价 `2^30` 证明的吞吐、相对单机单线程 CPU 的加速比和利用率。

## 配置

构建目录、二进制路径、平台和部署定义在：

[`configs/matrix.json`](configs/matrix.json)

默认路径与 Makefile 一致：`build/groth16/bin`、`build/groth16/data`、
`build/plonk/bin`、`build/plonk/data`。一台机器只有 GPU 或 DCU 之一，构建对应后端后
直接运行 `./scripts/... gpu|dcu`；如果实际构建目录不同，再修改
`platforms.*.groth16_bin`、`groth16_data`、`plonk_bin`、`plonk_data`。

## 结果

```text
results/<run_id>/
├── cases/<case_id>.json
├── summary.csv
├── report.md
├── report.json
├── samples/<case_id>/
├── audit/<case_id>.json
└── nodes/<node>/          # 分布式测试
    ├── node.json
    └── cases/<case_id>.json
```

## 说明

- 规模：填充后的 R1CS 约束行数 `2^k`，`k=20..26`；
- 吞吐：`m` 个 Prove 的 `T_max`，`m / T_max`；
- P1：每卡 `p` 个独立证明；P2：每卡 `p` 个独立证明；
- P3：每卡 16 个 `2^26` 证明，等价于每卡一个 `2^30` 证明；
- 利用率为真实采样；未达标配置标记 `util_unqualified`，不进入正式结论表；
- `GPU-sim` / `DCU-sim` 是 STARK 的 CPU 线程标签，不是真实 GPU/DCU。
