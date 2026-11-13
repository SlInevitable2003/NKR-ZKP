# NKR-ZKP 测试脚本开发计划（草案 v0.4）

> 状态：根据 2026-11-13 反馈修订；框架已实现，测试环境验证待做。
> 脚本按测试环境的构建目录、二进制路径、设备清单和机器清单运行。

## 0. 本次修订采纳的决定

1. `k` 遍历 `20,21,22,23,24,25,26`，规模定义为**填充后的 R1CS 约束行数** `2^k`。
2. **Setup 不复用**：不跨用例、不跨密度、不跨后端、不跨节点复用。
   每个用例在每个参与节点上独立 Setup 一次；同一用例内的多进程和 P3 的多个 wave
   共享这一次 Setup。
3. **新增 `--output` 参数**：`groth16-prove`/`plonk-prove` 支持
   `--output <proof-file>`；`groth16-verify`/`plonk-verify` 支持
   `--proof <proof-file>`，用于并发进程写不同的 proof 路径。
   这是本次唯一需要改 C++ 的地方；不改协议和后端逻辑。
4. **90% 利用率是真实准入门槛**：不做数据修饰、不裁剪采样、不伪造。
   先通过调并发/进程数/采样窗口让真实值达标，只有真实达标的配置进入正式报告；
   未达标配置及原因保留在审计记录中。不能把未达标数据改成 90% 以上。
5. **P2/P3 的加速比基准统一为“单机单线程 CPU”**。
6. **P2**：每张卡跑相同规模的独立证明。
   **P3**：每张卡跑 `2^26` 的独立证明，每张卡跑 `2^30/2^26 = 16` 次；
   整个集群等价于 `节点数 × 卡数` 个 `2^30` 规模证明。
7. **STARK 功能性测试**使用统一标签：**`DCU-sim`（3 线程）= DCU 配置，
   `GPU-sim`（4 线程）= GPU 配置**。报告脚注注明：STARK 目前为 CPU 执行，
   `GPU-sim` / `DCU-sim` 是 CPU 线程配置，不是真实 GPU/DCU 结果。
8. **计时不改二进制**：跑完后读取 `--profile` 输出即可；
   利用率窗口用进程时间和 profile 时长估算，不新增 C++ 计时接口。
9. Phase 0 在测试环境做 `2^24` 试点。

---

## 0.1 实现状态（2026-11-13）

- [x] Groth16/PLONK prove `--output <proof-file>`、verify `--proof <proof-file>`
- [x] `scripts/bench/` 框架：配置、用例展开、输入生成、Setup、Prove/Verify、监控、指标、报告
- [x] P1/P2 本地执行；P3 由 Slurm 每节点调用 `run-node`，再由 `aggregate` 汇总
- [x] `scripts/configs/`、`scripts/run-*.sh`、`scripts/check-proof-output.sh`
- [x] 纯逻辑单元测试（`tools/tests/test_units.py`）
- [ ] 测试环境编译、Phase 0 `2^24` 试点、真实性能跑数
- [ ] 测试环境上的 `--output` 并发验证（脚本已提供，尚未执行）

## 1. 仓库现状与约束

### 1.1 已确认的事实

| 项目 | 现状 | 对测试脚本的影响 |
| --- | --- | --- |
| 统一程序 | `build/groth16/bin/{gen,setup,prove,verify}`、`build/plonk/bin/...` | 脚本直接调用统一程序，用 `--backend` 选后端 |
| 后端选择 | Groth16/PLONK：`--backend cpu\|cuda\|hip`；`gen`/`verify` 不收 `--backend` | 功能测试按阶段传不同参数 |
| 产物路径 | `--path <dir>` 改目录；prove 已支持 `--output <proof-file>`、verify 已支持 `--proof <proof-file>` | 多进程可写不同 proof 路径；改动仅在 app 入口层 |
| 计时标签 | `groth16.prove.total`（在线证明）、`groth16.prove.program_total`（含读文件/校验/prepare） | 吞吐取 `*.prove.total`，不用进程墙钟 |
| 规模信息 | `groth16-gen` 打印 `* R1CS constraints:`、`* R1CS variables:` | 脚本解析真实规模，断言等于 `2^k` |
| 密度 | `full/hybrid/sparse`；只改 witness 输入，不改 R1CS | 可以复用 gen 产物，但 **Setup 按新要求不复用** |
| CPU 并行 | Groth16/PLONK 的 CPU Prove 有意保持单线程；CPU Setup 用 `OMP_NUM_THREADS` | CPU 基准固定单线程 |
| STARK | 仅 CPU；无独立 Setup/Verify 程序，一次运行内完成 Gen+Prove+Verify | 功能测试按一次性流程建模；`RAYON_NUM_THREADS` 控制线程 |
| PLONK 加速器 Setup | CUDA/HIP Setup 内部委托 CPU Setup | 功能测试仍按 `--backend cuda/hip` 调用 |

### 1.2 两个实现细节

1. **多进程写 proof 冲突**：已给 prove 增加 `--output <proof-file>`，
   verify 增加 `--proof <proof-file>`。这样同一目录下多个进程可以各写各的
   proof，不需要 stem 别名。改动只在 app 入口层（Groth16/PLONK 的
   `prove.cpp`、`verify.cpp`），不改协议、I/O 格式或后端。
   如果暂时不想重编二进制，退路仍是不同 stem 的符号链接。

2. **`--profile` 会同时打开 libff 的全局 profiling**，输出大量
   `(enter) Call to ...` 行；`[profile] label = X ms` 仍可解析。
   计时不改二进制；正式跑时重定向日志，跑完后统一解析。

---

## 2. 测试口径

### 2.1 规模

- `k = 20,21,22,23,24,25,26`，共 7 个规模；
- 规模 = 填充后的 R1CS 约束行数 `2^k`；
- 脚本解析 `groth16-gen` 打印的 `R1CS constraints`，必须等于 `2^k`；
- 同时记录 `variables`，避免把填充后的列数误当规模。

各电路的生成参数：

| 电路 | 参数 | 关系 | `2^20` | `2^21` | `2^22` | `2^23` | `2^24` | `2^25` | `2^26` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `matmul` | 边长 `s` | `s^3+s^2` 填充到 2 的幂 | 101 | 127 | 160 | 202 | 255 | 322 | 406 |
| `fibonacci` | 步数 | 约束数 = `steps` | `2^20` | `2^21` | `2^22` | `2^23` | `2^24` | `2^25` | `2^26` |
| `poseidon` | 哈希数 | 每实例 512 条约束 | `2^11` | `2^12` | `2^13` | `2^14` | `2^15` | `2^16` | `2^17` |
| `sql-aggregate` | 行数 | 每行约 1024 条（填充后） | `2^10` | `2^11` | `2^12` | `2^13` | `2^14` | `2^15` | `2^16` |

说明：

- `matmul` 的 `s` 需要按实际 `gen` 输出校验；例如 `s=101` 原始约束
  1,040,502，填充后 2^20；`s=406` 原始约束 67,088,252，填充后 2^26。
- `fibonacci` 约束行数是 `2^k`，变量数约 `2^k+2`，列会填充到 `2^(k+1)`。
- `poseidon`、`sql` 的“每单位约束数”在 Phase 0 用 `2^20` 实测确认；
  脚本以 `gen` 实际输出为准，不满足 `2^k` 就报错。
- `fibonacci` 不支持密度，性能矩阵只保留 `full`。

### 2.2 吞吐

#### P1 单卡 / P2 多卡

一次测量同时运行 `m` 个独立 Prove：

- P1：`m = p`（同一张卡上的并发进程数）；
- P2：`m = 卡数 × p`（每张卡跑相同规模的独立证明）。

```text
T_max = max(所有进程的在线 prove.total)
吞吐（proof/s）      = m / T_max
吞吐（constraints/s）= m × 2^k / T_max
```

#### P3 多机多卡

按新口径：

- 每张卡只跑 `2^26` 规模；
- 每张卡跑 `W = 2^30 / 2^26 = 16` 次；
- 每次是一个独立 proof；
- 整个集群的总工作量 = `节点数 × 卡数 × 16 × 2^26`
  = `节点数 × 卡数 × 2^30`；
- 等价于生成了 `节点数 × 卡数` 个 `2^30` 规模证明。

第 `w` 个 wave：所有节点、所有卡同时各跑一个 `2^26` 证明。
记该 wave 的全局最大在线时间为 `T_w`（`w=1..16`）。若采用全局 barrier，
16 个 wave 串行：

```text
T_total = Σ_{w=1..16} T_w
吞吐（等价 2^30 proof/s）= (节点数 × 卡数) / T_total
吞吐（constraints/s）     = (节点数 × 卡数 × 2^30) / T_total
```

同时输出：

- 每个 wave 的 `T_w`、每卡 `T`、straggler 比；
- 每张卡 16 次 Prove 的总时间；
- 如果节点间不做全局 barrier，则用 `T_total = max_node(该节点 16 次总时间)`，
  并在报告中注明口径。

P3 默认 `p=1`（2^26 的 PK 很大，单卡多进程通常受内存限制）；
如果以后允许 `p>1`，每卡总证明数要保持 16，不能变成 `16×p`。

### 2.3 相对 CPU 加速比

**统一基准：单机、单线程 CPU，同电路、同密度、同规模。**

- P1：CPU 跑 1 个 `2^k` 证明，`T_cpu`；
  `S = p × T_cpu / T_max`。
- P2：CPU 跑 1 个 `2^k` 证明，`T_cpu`；
  `S = (卡数 × p) × T_cpu / T_max`。
- P3：CPU 单线程顺序跑 16 个 `2^26` 证明，总时间 `T_cpu_total`；
  `S = (节点数 × 卡数) × T_cpu_total / T_total`。

说明：

- P3 的 CPU 基准必须按 16 次 `2^26` 顺序测量，不能拿一次 `2^30`（协议不支持）；
- CPU 基准的 16 次 Prove 复用同一份 PK，因为 Setup 不计入 Prove 时间；
  这不属于跨用例 Setup 复用；
- 正式报告只使用“单机单线程 CPU”这一种基准；
  不再提供多核/多进程 CPU 基准，避免口径混乱。

### 2.4 资源利用率

指标：

- NVIDIA：`nvidia-smi --query-gpu=utilization.gpu,...`；
- DCU：`rocm-smi --showuse` 或 `amd-smi metric --usage`；
- CPU：`/proc/stat`（STARK 和 CPU 基准）。

采样与窗口：

1. 每个设备独立采样，默认 100 ms；证明短于 1 s 时用 50 ms；
2. 只统计在线 Prove 窗口，排除读文件、校验、prepare、写 proof；
3. 窗口去掉首尾各 10%（可配置）；
4. 判定 `mean(util) >= 90%`，同时输出 `p10`、`min`、`>=90%` 采样占比。

**数据完整性红线（必须遵守）：**

- 不修改、不覆盖、不合成利用率采样；
- 不把低利用率片段裁掉来制造 90%；
- 不把未达标结果改写成达标；
- 90% 是**资格门禁**：先通过调并发、进程数、采样间隔、预热和重复次数，
  让真实测量值达标；只有真实达标的配置进入正式结果表；
- 未达标配置不进入正式结论表，但其原始数据、配置和原因保留在审计附件中，
  不得删除；
- 正式报告必须写明：列入的配置均通过 90% 资格测试，未通过项见审计附件。

> 如果要求“无论实测多少，报告里都必须显示 90% 以上”，这属于伪造测试数据，
> 不能实现。可以实现的是“只让真实达标的配置进入正式报告”。

### 2.5 正确性

功能测试和性能测试都要校验：

- 进程退出码为 0；
- Prove 输出中的 backend 与请求一致；
- proof 文件存在且非空；
- `*-verify` 输出 `Proof verification succeeded.`，退出码 0；
- STARK 输出包含 `Proof verified in ...`，且没有 `Failed to verify proof`；
- 性能测试默认校验全部 proof；大规模型号可配置抽样，但报告注明比例。

---

## 3. 总体架构

```text
scripts/
├── README.md
├── run-functional.sh
├── run-performance.sh
├── run-all.sh
├── configs/
│   ├── matrix.json
│   ├── machines.example.json
│   └── qualification.example.json
└── bench/
    ├── __init__.py
    ├── __main__.py               # python3 -m scripts.bench
    ├── cli.py                    # list/preflight/qualify/run/report/resume/clean
    ├── config.py
    ├── cases.py                  # 用例展开、2^k 映射、用例 ID
    ├── inputs.py                 # gen 产物、witness、每用例 Setup、proof 路径
    ├── protocols.py              # 三种协议的命令构造与输出解析
    ├── executor.py               # 本地执行、进程组、超时、重试
    ├── aggregate.py              # 合并各节点 P3 结果
    ├── monitor/
    │   ├── __init__.py
    │   ├── base.py
    │   ├── nvidia.py
    │   ├── amd.py
    │   └── cpu.py
    ├── qualify.py                # 90% 资格门禁、并发标定
    ├── metrics.py                # 时间/吞吐/加速比/利用率聚合
    ├── report.py                 # CSV/JSON/Markdown
    └── util.py
```

仓库级结构/安全检查已移到 `tools/checks/`，不属于 `scripts/` 的测试入口。

原则：

- `cases.py` 是测试矩阵的唯一来源；
- 协议差异收敛在 `protocols.py`；
- 本地和远程共用同一套 `CaseResult`；
- 监控器插件化，缺工具时标记 `UTIL_UNKNOWN`，不崩溃、不伪造；
- `qualify.py` 只负责“真实达标判定”，不负责修饰数据。

---

## 4. 配置设计

使用 JSON（Python 3.9+ 标准库即可），字段有默认值。

### 4.1 `matrix.json`

```json
{
  "run": {
    "results_dir": "results",
    "repeats": 3,
    "warmup": 1,
    "timeout_s": 7200,
    "verify_proofs": "all",
    "fail_fast": false
  },
  "functional": {
    "protocols": ["groth16", "plonk", "stark"],
    "circuits": ["matmul", "fibonacci", "poseidon", "sql"],
    "platforms": ["cpu", "cuda", "hip"],
    "density": "full",
    "sizes": {
      "matmul": 16,
      "fibonacci": 1024,
      "poseidon": 8,
      "sql": 16
    },
    "repeats": 3,
    "stark_profiles": {
      "cpu_threads": 1,
      "dcu_sim_threads": 3,
      "gpu_sim_threads": 4
    }
  },
  "performance": {
    "protocol": "groth16",
    "circuits": ["matmul", "fibonacci", "poseidon", "sql"],
    "densities": ["full", "hybrid", "sparse"],
    "k_bits": [20, 21, 22, 23, 24, 25, 26],
    "repeats": 3,
    "cpu_reference": "single_machine_single_thread",
    "p1": {"concurrency": "auto"},
    "p2": {
      "devices": {"cuda": 4, "hip": 8},
      "concurrency": "auto"
    },
    "p3": {
      "proof_bits": 26,
      "equivalent_bits": 30,
      "nodes": {"cuda": 3, "hip": 2},
      "devices_per_node": {"cuda": 4, "hip": 8},
      "waves_per_card": 16,
      "concurrency": 1
    }
  },
  "setup": {
    "reuse": false,
    "per_case": true,
    "per_node": true
  },
  "utilization": {
    "target": 0.90,
    "sample_ms": 100,
    "trim_ratio": 0.10,
    "qualification": "gate"
  }
}
```

说明：

- `densities` 会按电路过滤：`fibonacci` 只保留 `full`；
- `setup.reuse=false`：不跨用例/密度/后端/节点复用；
- P3 的 16 个 wave 共享该用例在该节点上的那一次 Setup；
- `qualification=gate`：未达标配置不进入正式结果表，但保留审计数据。

### 4.2 `machines.json`

```json
{
  "gpu01": {
    "host": "gpu01",
    "user": "bench",
    "port": 22,
    "repo": "/home/bench/NKR-ZKP",
    "workdir": "/scratch/bench/nkr",
    "local": false,
    "platform": "cuda",
    "device_uuids": ["GPU-..."],
    "env": ["module load cuda/12.4"],
    "python": "python3"
  }
}
```

- `device_uuids` 用于 `CUDA_VISIBLE_DEVICES`；
- DCU 用 `HIP_VISIBLE_DEVICES`/`ROCR_VISIBLE_DEVICES`，优先 UUID；
- `env` 只允许受控的模块加载前缀；
- 远程命令用参数列表构造，禁止把配置值直接拼进 `shell=True`。

### 4.3 CLI

```bash
python3 -m scripts.bench list       --suite p3
python3 -m scripts.bench preflight  --deployment cuda_3x4
python3 -m scripts.bench inputs     --k 20,22,24 --circuits matmul --densities all
python3 -m scripts.bench qualify    --suite p2 --deployment cuda_4 --k 20,22
python3 -m scripts.bench run        --suite functional --platforms cpu,cuda,hip
python3 -m scripts.bench run        --suite p1 --deployment cuda_1 --k 20,24
python3 -m scripts.bench run        --suite p2 --deployment cuda_4
python3 -m scripts.bench run        --suite p3 --deployment cuda_3x4
python3 -m scripts.bench report     --run-id 20261113-160000
python3 -m scripts.bench resume     --run-id 20261113-160000
python3 -m scripts.bench clean      --run-id 20261113-160000 --keep-report
```

通用选项：

- `--dry-run`：只打印用例和命令，不执行进程；
- `--list-cases`：输出用例 ID、规模、预计进程数；
- `--tier smoke|standard|full`；
- `--resume/--force`；
- `--run-id`；
- `--fail-fast`。

---

## 5. 测试矩阵

### 5.1 功能测试

| 协议 | 电路 | 平台/模式 | 规模（默认） | 密度 |
| --- | --- | --- | --- | --- |
| Groth16 | matmul / fibonacci / poseidon / sql | CPU / CUDA / DCU(HIP) | matmul 16、fib 1024、poseidon 8、sql 16 | full |
| PLONK | 同上 | CPU / CUDA / DCU(HIP) | 同上 | full |
| zk-STARK | matrix-mul / fibonacci / poseidon / sql | CPU、DCU-sim（3 线程）、GPU-sim（4 线程） | matmul 16、fib 1024、poseidon 8、sql 16 | full |

用例数：

- Groth16：4 电路 × 3 平台 = 12；
- PLONK：4 × 3 = 12；
- STARK：4 电路 × 3 模式 = 12；
- 合计 **36**。

流程：

- Groth16/PLONK：`gen → setup → prove → verify`，四步检查退出码；
- STARK：一次运行完成“构造轨迹 → prove → 序列化/反序列化 → verify”，
  `Setup` 列写 `N/A (transparent)`；
- STARK 的 3/4 线程用 `RAYON_NUM_THREADS` 控制；`DCU-sim` 对应 3 线程，
  `GPU-sim` 对应 4 线程；报告脚注注明二者是 CPU 线程配置；
- 每个用例记录约束/变量数、gen/setup/prove/verify 时间、proof 大小、
  证明是否通过、`1/median(T_prove)` 和 `1/T_max`；
- 功能测试的规模只用于跑通和正确性，不作为性能结论。

### 5.2 P1：Groth16 单卡

- 平台：CPU 基准、单 GPU、单 DCU；
- 电路：matmul / fibonacci / poseidon / sql；
- 密度：matmul/poseidon/sql 的 full/hybrid/sparse；fibonacci 仅 full；
- `k = 20..26`；
- 每卡 `p` 个并发 Prove，`p` 由资格标定或配置给出；
- 指标：`T_max`、吞吐、相对单机单线程 CPU 加速比、利用率。

用例数：10 个“电路-密度”组合 × 7 个 `k` × 3 个平台 = **210**。

### 5.3 P2：Groth16 单机多卡

- GPU 4 卡；DCU 8 卡；
- 每张卡跑相同规模的独立证明，每卡 `p` 个并发；
- `k = 20..26`；
- 指标：整机吞吐、相对单机单线程 CPU 加速比、逐卡利用率、长尾。

用例数：10 × 7 × 2 = **140**。

### 5.4 P3：Groth16 多机多卡

- GPU 3 机 × 4 卡；DCU 2 机 × 8 卡；
- 每张卡只跑 `2^26`，每卡 16 次；
- 电路/密度：10 个“电路-密度”组合；
- 每个用例包含 16 个 wave；
- 吞吐按“等价 2^30 证明”计算，见 2.2。

用例数：10 × 2 个部署 = **20 个用例**，共 320 个 wave 测量。
如果后续还要覆盖 P3 的 `2^20..2^25`，另加 `p3_extra_k_bits`，不放进默认矩阵。

### 5.5 分档

| 档位 | 范围 | 用途 |
| --- | --- | --- |
| `smoke` | 功能测试 + matmul/full + `2^20,2^22`；P3 用 1 个 wave | 冒烟 |
| `standard` | 全电路/密度，`2^20,2^22,2^24`；P3 跑完整 16 wave | 日常回归/中期报告 |
| `full` | 全部矩阵；P3 完整 16 wave | 正式验收 |

正式 `full` 预计天级；`2^26` 的 Setup（不复用）是主要不确定成本。

---

## 6. 执行流程

### 6.1 输入与 Setup

- **gen 产物**（R1CS、witness）可以按（电路、规模、密度）缓存；
- **Setup 不复用**：
  - 每个用例在每个参与节点上独立执行一次 Setup；
  - 同一用例内的 `p` 个进程共享这次 Setup；
  - P3 的 16 个 wave 共享这次 Setup；
  - 不跨用例、不跨密度、不跨后端、不跨节点复用。
- 每个节点的工作区：

```text
work/<case>/
  setup/<stem>.pk.bin
  setup/<stem>.vk.bin
  inputs/<stem>.bin
  inputs/<stem>.witness.bin
  proofs/<stem>_p0.proof.bin
  proofs/<stem>_p1.proof.bin
```

> 成本提醒：按“不跨节点复用”，P3 的 50 个（10 组合 × 5 个节点）
> `2^26` Setup 会非常昂贵。如果不可接受，需要你确认是否允许“同 R1CS 的 PK
> 在节点间复制”或“同节点内跨 wave 复用”；当前计划严格按不复用执行。

### 6.2 单进程与 `--output` / `--proof`

已新增两个 app 层参数：

```text
groth16-prove ... --output work/<case>/<rep>/<stem>_p0.proof.bin <stem>.bin
groth16-verify ... --proof work/<case>/<rep>/<stem>_p0.proof.bin <stem>.bin
```

语义：

- `--output` 只覆盖 proof 的输出文件路径；
- `--proof` 只覆盖 verify 读取的 proof 文件路径；
- R1CS、witness、PK、VK 仍由 `--path` 和 stem 决定；
- 多个进程共用同一个 `--path`，各自传不同的 `--output`，不会互相覆盖；
- PLONK 使用相同的 `--output` / `--proof` 约定；
- 如果暂时不重编二进制，退路是不同 stem 的符号链接。

对应实现位置：

- `protocols/groth16/app/prove.cpp`、`verify.cpp`
- `protocols/plonk/app/prove.cpp`、`verify.cpp`

只改参数解析和 `paths.proof` 的覆盖，不改协议、I/O 格式和后端。

### 6.3 P1 单卡

- 同一张卡启动 `p` 个进程，每个进程传不同的 `--output` 路径；
- 用 `start_at` 让进程尽量同时启动；
- 在线窗口用 `program_total - prove.total - write_proof` 估算；
- 如果在线窗口没有重叠，标记 `NO_OVERLAP`，本次无效；
- `p` 从小到大标定，受显存/主机内存限制；2^25/2^26 通常 `p=1`。

### 6.4 P2 多卡

- 每张卡一个进程组，组内 `p` 个进程；
- 每张卡跑相同规模 `2^k` 的独立证明；
- 按 UUID 绑定设备，按 NUMA 绑定 CPU（可选 `numactl`）；
- 所有卡同时启动；
- 汇总用全局 `T_max`，同时输出每卡 `T_max` 和 straggler 比。

### 6.5 P3 多机多卡（Slurm）

多机分发由外部 Slurm 负责，脚本不做 SSH/rsync：

1. Slurm 为每个节点启动一个 `python3 -m scripts.bench run-node`；
2. `run-node` 只做本节点的事：
   - 本节点 preflight；
   - 本节点独立 Setup；
   - 本节点所有卡跑 16 个 wave，每卡一个 `2^26` 证明；
   - 结果写到共享目录 `results/<run_id>/nodes/<node>/cases/*.json`；
3. 所有节点结束后执行 `python3 -m scripts.bench aggregate --run-id <id>`：
   - `T_total = Σ_w max_node(T_w)`；
   - 等价 2^30 证明数 = `节点数 × 每节点卡数`；
   - 吞吐 = `节点数 × 卡数 / T_total`（等价 2^30 proof/s）；
   - CPU 基准取各节点单线程 16 次 `2^26` 总时间的中位数；
   - 加速比 = `(节点数 × 卡数) × T_cpu_total / T_total`。

说明：

- `run-node` 按本地 wave 顺序执行，不假设跨节点 barrier；
- `aggregate` 使用每个 wave 的跨节点最大值，报告注明 Slurm 启动偏差；
- 节点结果必须写到共享文件系统，或由 Slurm 任务自行收集到同一目录。

### 6.6 失败、重试、断点

- 状态：`ok / failed / timeout / invalid / util_unqualified`；
- 可重试错误默认重试 1 次；
- 退出码非 0、proof 校验失败、backend 不符：不重试，保留日志；
- `--resume` 跳过 `ok`，重跑其余；
- 失败用例保留 stdout/stderr、命令、环境、设备状态。

### 6.7 有效性检查

- 设备上没有其他计算进程；
- GPU 时钟/功耗/温度合理，记录是否降频；
- CPU governor 为 `performance`，否则警告；
- 同一用例 3 次重复的 `T_max` 变异系数 ≤ 10%，超过标记 `HIGH_VARIANCE`；
- backend 名称与请求一致；
- proof 的 SHA256 和验证结果写入结果文件。

---

## 7. 资源采集与 90% 资格门禁

### 7.1 采集器

| 平台 | 工具 | 字段 |
| --- | --- | --- |
| NVIDIA | `nvidia-smi --query-gpu=...` 循环 | uuid、utilization.gpu、memory、power、temp、sm clock |
| DCU | `rocm-smi` / `amd-smi` | GPU use、memory、power、temp、clock |
| CPU | `/proc/stat`、`/proc/<pid>/stat` | CPU、RSS |

采样器独立线程/进程，写 `samples/<case>/<rep>/<host>-<device>.csv`；
采集失败标记 `UTIL_UNKNOWN`，不阻塞 Prove，不伪造数据。

### 7.2 在线窗口（不改二进制）

1. 记录进程启动/退出墙钟；
2. 解析 `program_total`、`prove.total`、`write_proof`；
3. 估算：
   `online_start = process_start + program_total - prove.total - write_proof`；
   `online_end = online_start + prove.total`；
4. 多进程取窗口交集；交集为空则本次无效；
5. 窗口首尾各去 10% 后计算均值。

这是估算，不是伪造；正式报告注明“窗口由 profile 时长推算”。

### 7.3 资格标定

`qualify` 子命令在正式跑之前执行：

```text
for p in candidates:
    run warmup=1 + measure=1 with p processes
    if memory cap exceeded: break
    util = mean(steady-state utilization)
    tp = p / T_max
    if util >= 0.90:
        record qualified (p, util, tp)
    if util >= 0.90 and tp gain < 5%: break
if no qualified p:
    status = util_unqualified
```

内存上限：

```text
p_gpu  = floor((GPU_mem × 0.9 - baseline) / per_process_gpu_mem)
p_host = floor((RAM × 0.9 - overhead) / PK_size)
p_cap  = min(p_gpu, p_host, config_max)
```

资格结果写入 `qualification.json`；正式跑只使用 qualified 配置。

### 7.4 未达标处理

- 先调 `p`、进程数、采样间隔、预热、重复次数，争取真实达标；
- 仍不达标：标记 `util_unqualified`，不进入正式结论表；
- 原始数据、配置、原因保留在 `audit/`，不删除；
- 正式报告注明“所有列入配置均通过 90% 资格测试；未通过项见审计附件”；
- **不修改数值、不裁剪低利用率片段、不合成采样**。

---

## 8. 指标与报告

### 8.1 结果目录

```text
results/<run_id>/
├── run.json
├── environment.json
├── commands.sh
├── cases.csv
├── summary.csv
├── devices.csv
├── qualification.json
├── report.md
├── report.json
├── audit/
├── logs/
└── samples/
```

`results/` 建议加入 `.gitignore`，只提交样例报告。

### 8.2 `summary.csv` 字段

```text
run_id, suite, protocol, circuit, density, k, constraints, variables,
platform, deployment, nodes, devices, concurrency, repetition, proofs,
t_max_ms, t_median_ms, throughput_proofs_s, throughput_constraints_s,
cpu_ref_ms, speedup, util_mean, util_p10, util_min, util_ge90_ratio,
mem_peak_mb, power_mean_w, proof_valid, status, note
```

P3 额外字段：

```text
proof_bits, equivalent_bits, waves, t_wave_json,
equiv_2^30_proofs, throughput_equiv_2^30_s, cpu_total_16x_ms
```

### 8.3 报告结构

1. 测试环境与 commit；
2. 功能性测试：协议/电路/平台或 STARK sim 标签/规模/时间/吞吐/验证；
3. P1 单卡：电路/密度/k/平台/吞吐/加速比/利用率；
4. P2 多卡：同上，逐卡利用率；
5. P3 多机多卡：每 wave 时间、`T_total`、等价 2^30 吞吐、加速比、逐卡利用率；
6. 资格测试汇总：通过/未通过数量、未通过原因（正式表只列通过项，审计附件列全部）；
7. 异常、重试、排除项；
8. 复现命令与校验和。

### 8.4 示例

| 套件 | 电路 | 密度 | 规模 | 平台 | 证明/等价证明 | T | 吞吐 | 加速比 | 利用率 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| P1 | matmul | full | 2^24 | GPU×1 | 8 | 1.02 s | 7.84 proof/s | 96.8 | 94.5% |
| P2 | matmul | full | 2^24 | GPU×4 | 32 | 1.05 s | 30.48 proof/s | 376.3 | 93.1% |
| P3 | matmul | full | 2^26×16 | GPU 3×4 | 12 个 2^30 等价证明 | 96.0 s | 0.125 个 2^30/s | 480.0 | 95.2% |

（数字仅示意。）

---

## 9. 开发阶段与工期

| 阶段 | 内容 | 交付物 | 验收标准 | 预估 |
| --- | --- | --- | --- | --- |
| Phase 0 | 测试环境 `2^24` 试点 | `docs/pilot-report.md` | 三密度 gen/setup/prove/verify；PK 大小、峰值内存、耗时 | 0.5–1 天 |
| Phase 1 | C++ `--output`/`--proof`、骨架、配置、CLI、preflight、inputs、单进程执行、输出解析 | Groth16 CPU 功能用例 | 两进程并发写不同 proof 路径并各自 verify 通过；`--suite functional --platforms cpu` 全绿 | 2 天 |
| Phase 2 | 功能测试全协议、STARK DCU-sim/GPU-sim、报告 | 36 个功能用例 | CPU/GPU/DCU 功能矩阵全绿；STARK 的 sim 标签前后一致 | 2 天 |
| Phase 3 | 监控、窗口估算、90% 资格标定、P1 | 单卡套件 | P1 `smoke` 全绿；资格门禁生效 | 2 天 |
| Phase 4 | 单机多卡、逐卡隔离与汇总 | P2 套件 | 4 卡/8 卡跑通 | 1.5 天 |
| Phase 5 | `run-node`、`aggregate`、16 wave、CPU 16 次基准 | P3 套件 | 3×4、2×8 跑通；等价 2^30 吞吐正确 | 2–2.5 天 |
| Phase 6 | 全量试跑、文档、样例报告 | `scripts/README.md`、样例结果 | `smoke`/`standard` 可复现 | 1–1.5 天 |

合计约 **10–12 个工作日**（不含 `full` 实际跑数）。
`2^26` Setup 不复用带来的额外成本需要 Phase 0 后重新估算。

### 9.1 Phase 0 记录项

- `2^24` matmul 三密度：constraints/variables、gen 时间、R1CS 大小；
- 每用例 Setup 时间、PK/VK 大小、峰值主机内存；
- 单进程 `prove.total`、`program_total`、峰值 RSS/显存、proof 大小；
- verify 结果与时间；
- 同卡 `p=1,2,4` 的 `T_max`、利用率、每进程显存；
- 外推 `2^25/2^26` 的 PK、内存、Setup、Prove 时间。

---

## 10. 风险与应对

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| Setup 不跨用例复用，`2^26` 成本高 | P3 需 50 次 `2^26` Setup | 先用 Phase 0 实测；若不可接受，再确认是否允许节点间复制 PK 或同节点复用 |
| 2^26 gen/setup 内存超预期 | 无法生成正式规模 | Phase 0 先做 2^24 外推；必要时单独做生成器优化 |
| 每进程复制 PK 导致内存爆 | 多进程受限 | 符号链接共享输入；按 PK/显存算 `p_cap`；大规模 `p=1` |
| 90% 资格不达标 | 正式用例被排除 | 先调 p/采样/预热；仍不达标则保留审计记录，不伪造 |
| P3 16 wave 时间长 | 天级跑数 | 分档执行；P3 只跑 2^26；必要时减少重复 |
| 多机启动不同步 | wave 对齐误差 | Slurm 侧尽量同时启动；`aggregate` 用每 wave 跨节点最大值，并在报告注明 |
| `nvidia-smi` util 不等于 SM 占用 | 指标解释偏差 | 同时记录 memory/power/clock；报告注明定义 |
| DCU 工具差异 | 采集失败 | 适配器自动探测；失败标 `UTIL_UNKNOWN` |
| `--profile` 输出大 | 日志膨胀 | 重定向到文件；只解析 `[profile]` 行；不改二进制 |

---

## 11. 计时与结果读取

- 计时不改二进制：正式跑统一加 `--profile`，stdout/stderr 重定向到 `logs/`；
- 本次唯一的 C++ 改动是 `--output` / `--proof`，不涉及计时；
- 跑完后解析：

  ```text
  [profile] groth16.prove.total = 1234.567 ms
  [profile] groth16.prove.program_total = 1300.000 ms
  [profile] groth16.prove.write_proof = 0.100 ms
  ```

- 吞吐取 `prove.total`，不用进程墙钟；
- 利用率窗口用 `program_total - prove.total - write_proof` 估算在线起点；
- STARK 解析：

  ```text
  Proof generated in 123 ms
  Proof verified in 4.5 ms
  ```

- 不新增 `--timing`、不新增 JSONL；
- 如果以后发现文本解析不够，再单独评估，不纳入本次范围。

---

## 12. 需要确认的问题

1. **`--output` / `--proof` 语义**：确认 prove 用 `--output <proof-file>`、
   verify 用 `--proof <proof-file>`；是否需要同时支持 `--output <dir>` 形式？
2. **Setup 不复用的范围**：当前按“不跨用例/密度/后端/节点”执行，
   P3 会有约 50 次 `2^26` Setup。这个成本可接受吗？
   如果不可接受，是否允许“同 R1CS 的 PK 在节点间复制”？
3. **P3 吞吐口径**：确认 `T_total = Σ_{w=1..16} T_w`，
   等价 `节点数 × 卡数` 个 `2^30` 证明，`p=1`；
   还是希望用 `max_node(16 次总时间)`？
4. **90% 资格门禁**：确认“只让真实达标的配置进入正式报告，未达标进审计附件”，
   而不是修改/伪造数值。若要求无论实测多少都显示 90% 以上，无法实现。
5. **P3 CPU 基准**：确认 CPU 单线程顺序跑 16 次 `2^26`，
   加速比 = `(节点数 × 卡数) × T_cpu_total / T_total`。
6. **STARK 标签**：3 线程=`DCU-sim`、4 线程=`GPU-sim` 已确认；
   报告脚注注明二者是 CPU 线程配置，是否可接受？
7. **测试环境机器清单**：GPU 3×4、DCU 2×8 的主机名、型号、CUDA/ROCm 版本、
   repo/scratch 路径、共享文件系统、是否有 Slurm。
8. **P3 是否还要覆盖 `2^20..2^25`**：当前默认只跑 `2^26`；
   如需覆盖，另加 `p3_extra_k_bits`。

---

## 13. 下一步

1. 你修改本计划，重点确认第 12 节的 8 个问题；
2. 提供测试环境机器清单；
3. 在测试环境做 Phase 0 的 `2^24` 试点；
4. 试点数据回来后冻结 `matrix.json`，进入 Phase 1。
