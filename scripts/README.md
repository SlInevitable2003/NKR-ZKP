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
python3 -m scripts.bench qualify --suite p1 --tier smoke --run-id demo
python3 -m scripts.bench run --suite p1 --tier smoke --run-id demo

# 全量
./scripts/run-all.sh --tier full

# 验证两个 prove 进程可并发写不同 proof 路径
./scripts/check-proof-output.sh matmul_10
```

`python3 -m scripts.bench` 必须在仓库根目录执行；包装脚本会自动 `cd` 到根目录。
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
| `run` | 执行用例，写 `cases/*.json`、`summary.csv`、`report.md` |
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
    ├── remote.py
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
└── cpu-reference/...
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

## 多机

P3 的 `cuda_3x4` / `hip_2x8` 需要 `scripts/configs/machines.json`。先从
`machines.example.json` 复制并填写主机名、repo、scratch、GPU/DCU UUID。
驱动节点会为每个节点写 plan、通过 SSH 启动 `_worker`、用 rsync 回收每个
wave 的结果；Setup 在每个节点本地执行。使用前请先在测试环境做 `preflight`。

## `--output` / `--proof`

Prove 和 Verify 已支持：

```bash
groth16-prove ... --output /path/to/proof.bin <stem>.bin
groth16-verify ... --proof /path/to/proof.bin <stem>.bin
```

PLONK 同样适用。这样 P1/P2 的多进程可以共用 `--path`，各写各的 proof。
