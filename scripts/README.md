# NKR-ZKP 测试脚本

本目录只包含面向测试环境的功能测试、性能测试、资源采集和报告入口。
仓库结构/安全检查位于 `tools/checks/`，harness 自身的纯逻辑单元测试位于
`tools/tests/`，都不放进 `scripts/`。

框架使用 Python 3.9+ 标准库，不依赖 PyYAML/pandas 等第三方包；所有产物写入
`results/<run_id>/`，原始设备采样、日志、命令和环境指纹都会保留。

## 快速开始

```bash
# 只列用例，不执行
python3 -m scripts.bench list --suite functional

# 检查二进制和采集工具
python3 -m scripts.bench preflight --suite functional

# 生成/缓存电路输入（不跑 Prove）
python3 -m scripts.bench inputs --suite p1 --circuit matmul --k 20

# 功能测试
./scripts/run-functional.sh --tier smoke

# 单卡性能测试：先标定并发，再用同一个 run-id 正式跑
python3 -m scripts.bench qualify --suite p1 --tier smoke --run-id p1-calib
python3 -m scripts.bench run --suite p1 --tier smoke --run-id p1-calib

# 功能 + P1 + P2（P3 由 Slurm 的 run-node 单独执行）
./scripts/run-all.sh --tier full

# 验证两个 prove 进程可并发写不同 proof 路径
./scripts/check-proof-output.sh matmul_10
```

`python3 -m scripts.bench` 必须在仓库根目录执行；包装脚本会自动 `cd` 到根目录。

`--tier` 只控制测试范围，不是协议或测试类型：

- `smoke`：快速自检，只跑功能测试 + `matmul/full` 的 `2^20,2^22`，
  P3 只跑 1 个 wave，重复 1 次。用于确认二进制、设备、采集和报告链路能跑通，
  **不作为正式验收数据**。
- `standard`：日常回归/中期报告，覆盖 `2^20,2^22,2^24`。
- `full`：正式全量，覆盖 `2^20–2^26` 和全部电路/密度。

`run-id` 只是结果目录名，例如 `p1-calib`，可以任意取；不是特殊模式。
如果测试环境的构建目录不是 `build/groth16[-cuda|-hip]` / `build/plonk[-cuda|-hip]`，
请在 `scripts/configs/matrix.json` 的 `platforms.*.groth16_bin`、`groth16_data`、
`plonk_bin`、`plonk_data` 中改成实际路径。

## 子命令

| 命令 | 说明 |
| --- | --- |
| `list` | 展开并打印用例 |
| `preflight` | 检查二进制、`nvidia-smi`/`rocm-smi`/`amd-smi` |
| `inputs` | 生成并缓存 R1CS/witness（Setup 不在这一步） |
| `qualify` | 对 P1/P2 标定并发度，使真实利用率达到 90% |
| `run` | 执行功能/P1/P2 用例，写 `cases/*.json`、`summary.csv`、`report.md` |
| `run-node` | 执行本节点的 P3 份额，供 Slurm 每节点调用 |
| `aggregate` | 合并各节点 P3 结果，生成最终报告 |
| `report` | 从已有 run 重新生成报告 |
| `clean` | 删除 run 目录（限制在 `results/` 下） |

过滤参数：`--suite functional|p1|p2|p3|all`、`--tier smoke|standard|full`、
`--circuit`、`--density`、`--k`、`--platform`、`--deployment`、`--limit`。

## 目录

```text
scripts/
├── run-functional.sh
├── run-performance.sh
├── run-all.sh
├── check-proof-output.sh
├── configs/
│   ├── matrix.json
│   ├── machines.example.json
│   └── qualification.example.json
└── bench/
    ├── cli.py
    ├── config.py
    ├── cases.py
    ├── protocols.py
    ├── inputs.py
    ├── executor.py
    ├── runner.py
    ├── aggregate.py
    ├── qualify.py
    ├── metrics.py
    ├── report.py
    └── monitor/
```

## 结果目录

```text
results/<run_id>/
├── run.json
├── environment.json
├── qualification.json     # qualify 后生成
├── cases/<case_id>.json
├── summary.csv
├── report.md
├── report.json
├── audit/<case_id>.json
├── samples/<case_id>/repNNN.csv
├── work/<case_id>/...
├── cpu-reference/...
└── nodes/<node>/          # P3: run-node 写的本节点结果
    ├── node.json
    ├── cases/<case_id>.json
    └── samples/...
```

## 口径

- 规模：填充后的 R1CS 约束行数 `2^k`，`k=20..26`；
- 吞吐：`m` 个 Prove 的 `T_max`，`m / T_max`；
- P1：每卡 `p` 个独立证明，`m=p`；
- P2：每卡 `p` 个独立证明，`m=卡数×p`；
- P3：每卡 16 个 `2^26` 证明，等价于每卡一个 `2^30` 证明；
  `T_total = Σ T_wave`，吞吐 = `节点数×卡数 / T_total`（等价 2^30 proof/s）；
- CPU 基准：单机单线程；P3 的 CPU 基准是 16 次 `2^26` 的顺序总时间。

## 资源利用率

- 只采样在线 Prove 窗口；窗口由进程时间和 `--profile` 时长估算；
- 利用率是真实采样，不修改、不裁剪成目标值；
- `qualify` 通过调整并发度让真实值达到 90%；未达标配置状态为
  `util_unqualified`，不进入正式结论表，但原始数据保留在 run 目录；
- 正式报告只列通过资格测试的配置，并在 `report.md` 中注明筛选口径。

## STARK 标签

STARK 目前只有 CPU 实现。功能测试里的 3 线程/4 线程统一写为
`DCU-sim`（3 线程）和 `GPU-sim`（4 线程），报告脚注注明这是 CPU 线程配置，
不是真实 GPU/DCU 结果。

## P3 / Slurm

P3 不做 SSH/rsync 编排：每个节点由 Slurm 启动一个 `run-node`，只跑本节点的
16 个 wave，结果写到共享目录下的 `results/<run_id>/nodes/<node>/`。所有节点
完成后，在任意节点执行 `aggregate` 汇总。

```bash
# Slurm 任务内，每个节点跑自己的份额
python3 -m scripts.bench run-node --deployment cuda_3x4 --node gpu01 --run-id p3-20261113

# 全部节点结束后汇总
python3 -m scripts.bench aggregate --run-id p3-20261113
```

`run-node` 会在本节点独立执行 Setup，不跨节点复用。`machines.example.json`
只用于可选的设备 UUID 映射；不配置时按节点上的设备编号运行。

## `--output` / `--proof`

Prove 和 Verify 已支持：

```bash
groth16-prove ... --output /path/to/proof.bin <stem>.bin
groth16-verify ... --proof /path/to/proof.bin <stem>.bin
```

PLONK 同样适用。这样 P1/P2 的多进程可以共用 `--path`，各写各的 proof。
