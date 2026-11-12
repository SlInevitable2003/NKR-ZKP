# NKR-ZKP

NKR-ZKP 是一个实验性的多协议、多后端零知识证明框架。当前项目将协议语义与可复用的计算算子分离：

```text
common/
  include/                 共享 BinaryArchive、Profile 等主机侧工具
  src/                     共享主机侧工具实现

operators/
  cuda/                    共享的 BN254 CUDA 域、曲线、NTT、MSM 算子
  hip/                     共享的 BN254 HIP 域、曲线、NTT、MSM 算子

protocols/
  groth16/
    app/                   仅含 gen/setup/prove/verify 的 .cpp 入口
    common/                协议类型、I/O、验证及后端注册
    backends/{cpu,cuda,hip}
  plonk/
    app/                   仅含统一的 gen/setup/prove/verify .cpp 入口
    common/                算术化、协议类型、I/O、API 及后端注册
    backends/{cpu,cuda,hip}
  stark/winterfell/

third_party/lightzk/       内置的 libsnark/libff/libfqfft/CGBN 源码快照
docs/                      架构、源码来源及协议文档
```

CUDA 和 HIP 算子由 Groth16 与 PLONK 共用。协议专属逻辑保留在各自的协议后端中：Groth16 负责 R1CS、QAP 和 SpMV，PLONK 负责置换、商多项式、线性化与打开证明等逻辑。CUDA 与 HIP 实现相同的算子边界，但分别保留独立内核，以便针对各平台单独优化和审计。

## 当前状态

- Groth16 已有可用的 CPU、CUDA 和 HIP 后端。CPU 后端始终参与构建；配置时可以选择 CUDA 或 HIP 加速后端。
- PLONK 已实现独立的三线 Plonkish 算术化 Manager、SHA3-256 Transcript、CPU Setup/Prove/Verify、CUDA Prove 和面向 DCU 的 HIP Prove；电路、见证、SRS、PK/VK 与证明均通过共享 `BinaryArchive` 读写。CUDA/HIP Setup 复用 CPU Setup。
- Winterfell 仍作为独立的 Rust 实现保留，目前只有 CPU 实现；在 NVIDIA GPU 或 DCU 机器上运行也仍然使用 CPU。

以下构建与运行命令均在仓库根目录执行，产物默认位于 `build/` 下。

## Groth16 构建与运行

Groth16 始终生成同一组四个程序。CPU 后端始终参与构建，可按需叠加 CUDA 或 HIP 加速后端：

```text
build/groth16/bin/groth16-gen
build/groth16/bin/groth16-setup
build/groth16/bin/groth16-prove
build/groth16/bin/groth16-verify
```

```bash
make groth16-cpu
make groth16-cuda CUDA_ARCH=80          # 额外提供 --backend cuda
make groth16-hip HIP_ARCH=<gfx-arch>    # 额外提供 --backend hip（dcu 为别名）
```

CUDA 默认位于 `/usr/local/cuda`，可用 `CUDA_HOME` 覆盖；HIP 构建要求 CMake 找到 ROCm 工具链。`gen` 和 `verify` 与后端无关；`setup`、`prove` 通过 `--backend cpu|cuda|hip` 选择实现。

### 1. 电路生成

`groth16-gen` 生成 R1CS 和满足该 R1CS 的见证，默认写入 `build/groth16/data`。支持的电路：

| 电路 | 命令 | 公开输入 | 说明 |
| --- | --- | --- | --- |
| 矩阵乘法 | `groth16-gen matmul <size>` | `A`、`B`、`C` 各 `size²` 个元素 | 约束 `C = A × B`；支持密度 |
| Fibonacci | `groth16-gen fibonacci <steps>` | `F0`、`F1`、`F_steps` | 中间项为私有见证；`steps >= 2`；不支持密度 |
| Poseidon | `groth16-gen poseidon [count]` | `count` 个摘要 | 每个实例的左右原像为私有见证；`count` 默认为 1；支持密度 |
| SQL 聚合 | `groth16-gen sql-aggregate <rows>` | Merkle root、target category、COUNT、SUM | 固定查询 `WHERE category = target`；`rows` 为不小于 2 的 2 的幂；支持密度 |

**选项**

| 选项 | 取值 | 说明 |
| --- | --- | --- |
| `--density` | `full` / `hybrid` / `sparse` | 输入密度，默认 `full`；大小写不敏感，也接受 `--density=sparse`；仅对 `matmul`、`poseidon`、`sql-aggregate` 生效 |
| `--profile` | 无 | 输出电路生成各阶段耗时 |

**密度档位**

| 档位 | 非零输入占比 | 其余输入 |
| --- | --- | --- |
| `full`（FULL） | 100% | 无零元素 |
| `hybrid`（HYBRID） | 50%（`ceil(N/2)`） | 置零并随机打散 |
| `sparse`（SPARSE） | 10%（`ceil(N/10)`） | 置零并随机打散 |

密度作用范围：

- `matmul`：`N = size²`，A、B 各自独立采样；
- `poseidon`：`N = 2 × count`，每个哈希的左右输入独立采样；
- `sql-aggregate`：`N = rows`，只作用于 amount 列，category 保持 0..15 随机；
- 非 `full` 档位会在产物文件名后追加 `_hybrid` / `_sparse`；`fibonacci` 传入非 `full` 值会报错。

**产物**

```text
build/groth16/data/<stem>.bin          R1CS
build/groth16/data/<stem>.witness.bin  满足 R1CS 的见证（含公开输入）
```

`<stem>` 为 `matmul_<size>`、`fibonacci_<steps>`、`poseidon_<count>` 或 `sql_aggregate_<rows>`，非 full 档位再追加密度后缀。

**示例**

```bash
G16=build/groth16/bin
"$G16/groth16-gen" --profile matmul 100
"$G16/groth16-gen" --profile fibonacci 16
"$G16/groth16-gen" --profile poseidon 8
"$G16/groth16-gen" --profile sql-aggregate 4

"$G16/groth16-gen" --profile --density sparse matmul 100
"$G16/groth16-gen" --profile --density hybrid poseidon 8
"$G16/groth16-gen" --profile --density=sparse sql-aggregate 4
```

### 2. Setup & Prove

Groth16 没有通用 SRS：`setup` 由 R1CS 直接生成 PK/VK，`prove` 用 PK 和见证生成证明。两者都支持 `--backend`、`--path <dir>`、`--profile`。

| 程序 | 输入 | 输出 |
| --- | --- | --- |
| `groth16-setup` | `<stem>.bin` | `<stem>.pk.bin`、`<stem>.vk.bin` |
| `groth16-prove` | `<stem>.bin`、`<stem>.witness.bin`、PK | `<stem>.proof.bin` |

```bash
G16=build/groth16/bin

# CPU
"$G16/groth16-setup" --profile --backend cpu matmul_100.bin
"$G16/groth16-prove" --profile --backend cpu matmul_100.bin

# CUDA（使用 CUDA 构建）
"$G16/groth16-setup" --profile --backend cuda matmul_100.bin
"$G16/groth16-prove" --profile --backend cuda matmul_100.bin

# HIP（使用 HIP 构建）
"$G16/groth16-setup" --profile --backend hip matmul_100.bin
"$G16/groth16-prove" --profile --backend hip matmul_100.bin
```

- 密度产物使用对应的 stem（如 `matmul_100_sparse.bin`），setup/prove 会自动沿用文件名前缀；
- `--path <dir>` 覆盖默认产物目录，此后 setup/prove/verify 必须指向同一目录；
- PK/VK/证明使用同一文件格式；`gen` 与后端无关，切换后端不需要重新生成电路；
- 性能计时分层：`groth16.prove.program_total` 是包含文件读写、校验和 prepare 的程序端到端时间，`groth16.prove.prepare_backend` 覆盖设备布局、NTT 幂表等与 witness 无关的预计算及 PK 上传，`groth16.prove.total` 只包含在线证明生成。

需要一条命令完成构建和端到端运行时：

```bash
make groth16-test-cpu PROFILE=1 GROTH16_CPU_MATRIX_SIZE=100
make groth16-test-cuda PROFILE=1 CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=100
make groth16-test-hip PROFILE=1 HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=100
```

`groth16-dcu` 和 `groth16-test-dcu` 是对应 HIP 目标的兼容性别名。

### 3. Verify

`groth16-verify` 读取电路、见证、VK 和证明。公开输入从见证的前 `public_inputs` 项读取，因此验证命令只接收 stem，并支持 `--path <dir>` 和 `--profile`：

```bash
G16=build/groth16/bin
"$G16/groth16-verify" --profile matmul_100.bin
"$G16/groth16-verify" --path /tmp/groth16-data matmul_100.bin
"$G16/groth16-verify" --profile matmul_100_sparse.bin
```

- `verify` 与 `gen` 一样不接收 `--backend`；
- 默认产物目录为 `build/groth16/data`；
- 成功输出 `Proof verification succeeded.`，失败输出 `Proof verification FAILED.` 并返回非零。

## PLONK 构建与运行

PLONK 与 Groth16 使用相同的四阶段程序结构，CPU 始终参与构建，可叠加 CUDA 或 HIP：

```text
build/plonk/bin/plonk-gen
build/plonk/bin/plonk-setup
build/plonk/bin/plonk-prove
build/plonk/bin/plonk-verify
```

```bash
make plonk-cpu
make plonk-cuda CUDA_ARCH=120            # 额外提供 --backend cuda
make plonk-hip HIP_ARCH=<gfx-arch>       # 额外提供 --backend hip（dcu 为别名）
```

`gen` 和 `verify` 与后端无关；`setup`、`prove` 通过 `--backend cpu|cuda|hip` 选择实现。CUDA/HIP Setup 复用 CPU Setup，并在 PK 中写入后续证明需要的 NTT 幂表；CUDA/HIP Prove 在设备上完成 NTT、MSM、域运算、前缀乘积、多项式求值和综合除法，Transcript 与挑战生成保留在 CPU。HIP 实现以 DCU 兼容性为优先：使用无硬件进位链假设的朴素大整数运算，Fp2、MSM、scan、NTT 和多项式算子不依赖 warp shuffle 或 cooperative groups，`make check` 会静态检查这些限制。

### 1. 电路生成

`plonk-gen` 生成 PLONK 电路和满足该电路的见证，默认写入 `build/plonk/data`。支持的电路：

| 电路 | 命令 | 公开输入 | 说明 |
| --- | --- | --- | --- |
| 矩阵乘法 | `plonk-gen matmul <size>` | `A`、`B`、`C` 各 `size²` 个元素 | 约束 `C = A × B`；支持密度 |
| Fibonacci | `plonk-gen fibonacci <steps>` | `F0`、`F1`、`F_steps` | 中间项为私有见证；`steps >= 2`；不支持密度 |
| Poseidon | `plonk-gen poseidon [count]` | `count` 个摘要 | 每个实例的左右原像为私有见证；`count` 默认为 1；支持密度 |
| SQL 聚合 | `plonk-gen sql-aggregate <rows>` | Merkle root、target category、COUNT、SUM | 与 Groth16 相同的固定查询；`rows` 为不小于 2 的 2 的幂；支持密度 |

Poseidon 与 Groth16 共用 `common/include/crypto/poseidon.hpp` 的 BN254 参数（`x^5`、`t=3`、`R_F=8`、`R_P=57`）。SQL 聚合的私有表由 Poseidon Merkle root 绑定。

**选项**

| 选项 | 取值 | 说明 |
| --- | --- | --- |
| `--density` | `full` / `hybrid` / `sparse` | 输入密度，默认 `full`；大小写不敏感，也接受 `--density=sparse`；仅对 `matmul`、`poseidon`、`sql-aggregate` 生效 |
| `--profile` | 无 | 输出电路生成各阶段耗时 |

密度档位与 Groth16 相同（FULL 100%、HYBRID 50%、SPARSE 10%，其余置零并随机打散，非零个数为 `ceil(N × 密度)`）：

- `matmul`：`N = size²`，A、B 独立采样；
- `poseidon`：`N = 2 × count`；
- `sql-aggregate`：`N = rows`，只作用于 amount 列；
- 非 `full` 档位文件名追加 `_hybrid` / `_sparse`；`fibonacci` 传入非 `full` 值会报错。

**产物**

```text
build/plonk/data/<stem>.bin           PLONK 电路
build/plonk/data/<stem>.witness.bin   执行轨迹见证
```

`<stem>` 规则与 Groth16 相同。

**示例**

```bash
PLONK_BIN=build/plonk/bin
"$PLONK_BIN/plonk-gen" --profile matmul 79
"$PLONK_BIN/plonk-gen" --profile fibonacci 10
"$PLONK_BIN/plonk-gen" --profile poseidon 2
"$PLONK_BIN/plonk-gen" --profile sql-aggregate 4

"$PLONK_BIN/plonk-gen" --profile --density sparse matmul 79
"$PLONK_BIN/plonk-gen" --profile --density hybrid poseidon 4
"$PLONK_BIN/plonk-gen" --profile --density=sparse sql-aggregate 4
```

### 2. Setup & Prove

`plonk-setup` 读取电路，生成 SRS、PK 和 VK；未提供 `--srs` 时现场生成并写出 `<stem>.srs.bin`。`plonk-prove` 读取 PK 和见证生成证明。两者都支持 `--backend`、`--path <dir>`、`--profile`；`setup` 额外支持 `--srs <file>`。

| 程序 | 输入 | 输出 |
| --- | --- | --- |
| `plonk-setup` | `<stem>.bin` | `<stem>.srs.bin`（未提供 `--srs` 时）、`<stem>.pk.bin`、`<stem>.vk.bin` |
| `plonk-prove` | `<stem>.bin`、`<stem>.witness.bin`、PK | `<stem>.proof.bin` |

```bash
PLONK_BIN=build/plonk/bin

# CPU
"$PLONK_BIN/plonk-setup" --profile --backend cpu matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend cpu matmul_79.bin

# CUDA（使用 CUDA 构建；Setup 内部委托 CPU）
"$PLONK_BIN/plonk-setup" --profile --backend cuda matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend cuda matmul_79.bin

# HIP（使用 HIP 构建；Setup 内部委托 CPU）
"$PLONK_BIN/plonk-setup" --profile --backend hip matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend hip matmul_79.bin
```

复用已有 SRS 时显式传入 `--srs`；SRS 必须覆盖目标电路的 domain size：

```bash
"$PLONK_BIN/plonk-gen" --profile fibonacci 10
"$PLONK_BIN/plonk-setup" --profile --backend cpu --srs matmul_79.srs.bin fibonacci_10.bin
"$PLONK_BIN/plonk-prove" --profile --backend cpu fibonacci_10.bin
```

- 密度产物使用对应的 stem（如 `matmul_79_sparse.bin`）；
- `--path <dir>` 覆盖默认产物目录，setup/prove/verify 必须使用同一目录；
- PK 中包含 Setup 预计算数据，更新程序后需要重新运行 Setup，不能复用旧 PK；
- CPU Setup 的 FixMSM 与专用 MSM 在调用层使用 OpenMP（`OMP_NUM_THREADS`），CPU Prove 保持单线程。

端到端与分层回归：

```bash
make plonk-test-cpu PROFILE=1 PLONK_CPU_MATRIX_SIZE=79
make plonk-test-cuda PROFILE=1 CUDA_ARCH=120 PLONK_CUDA_MATRIX_SIZE=79
make plonk-test-hip PROFILE=1 HIP_ARCH=<gfx-arch> PLONK_HIP_MATRIX_SIZE=79
make plonk-test-arithmetization
make plonk-test-cpu-setup
make plonk-test-cpu-prove-verify
```

`plonk-dcu` 和 `plonk-test-dcu` 是对应 HIP 目标的兼容性别名。

### 3. Verify

`plonk-verify` 读取电路、见证、VK 和证明，公开输入从见证的公开输入列读取。支持 `--path <dir>` 和 `--profile`，不接收 `--backend`：

```bash
PLONK_BIN=build/plonk/bin
"$PLONK_BIN/plonk-verify" --profile matmul_79.bin
"$PLONK_BIN/plonk-verify" --path /tmp/plonk-data matmul_79.bin
"$PLONK_BIN/plonk-verify" --profile poseidon_2.bin
```

- 默认产物目录为 `build/plonk/data`；
- 成功输出 `Proof verification succeeded.`，失败输出 `Proof verification FAILED.` 并返回非零；
- CPU、CUDA、HIP 生成/验证的电路、见证、SRS、PK/VK 和证明共用同一 `BinaryArchive` 格式。

更详细的算术化、Transcript、文件格式和 CPU 协议实现说明见 [protocols/plonk/README.md](protocols/plonk/README.md)。

## zk-STARK / Winterfell 构建与运行

zk-STARK 相关代码位于 `protocols/stark/winterfell`，是一个独立的 Rust workspace，包含 AIR、有限域与 FFT、Merkle 承诺、FRI、Prover、Verifier 和示例程序。它目前只有 CPU 实现，在 NVIDIA GPU 或 DCU 机器上运行也仍然使用 CPU。STARK 是透明证明系统，不需要可信设置。

构建与运行需要 Rust 1.87 和 Cargo，工具链版本记录在 `protocols/stark/winterfell/rust-toolchain.toml`：

```bash
make stark THREADS=8        # 等价于 cargo build --release --features concurrent -p examples
make stark-matmul THREADS=8 MATRIX_SIZE=8 DENSITY=sparse PROFILE=1
```

可执行文件位于 `protocols/stark/winterfell/target/release/winterfell`；`THREADS` 通过 `RAYON_NUM_THREADS` 控制 Rayon 并行度。

### 1. 电路生成

STARK 没有独立的电路文件：示例程序在运行时构造执行轨迹（trace）。支持密度档位的电路：

| 示例 | 命令 | 公开输入 | 说明 |
| --- | --- | --- | --- |
| 矩阵乘法 | `winterfell matrix-mul -n <size>` | `A`、`B`、`C` | 别名 `matmul`；`size` 为不小于 2 的 2 的幂；支持密度 |
| Poseidon | `winterfell poseidon -n <hashes>` | 输入对和摘要 | `hashes` 为 2 的幂；支持密度；f128 域参数 |
| SQL 聚合 | `winterfell sql -n <rows>` | target、COUNT、SUM、链摘要 | `rows` 为不小于 2 的 2 的幂；支持密度；Poseidon 哈希链承诺 |
| Fibonacci | `winterfell fib -n <length>` | `F0`、`F1`、结果 | 现有示例；`length` 为不小于 16 的 2 的幂；不支持密度 |

**选项**（全局选项可放在子命令前后）

| 选项 | 取值 | 说明 |
| --- | --- | --- |
| `--density` | `full` / `hybrid` / `sparse` | 输入密度，默认 `full`；大小写不敏感；仅对 `matrix-mul`、`poseidon`、`sql` 生效 |
| `--profile` | 无 | 输出输入生成、轨迹构造、证明生成、验证等阶段耗时 |
| `--hash_fn` | `blake3_192` / `blake3_256` / `sha3_256` | 默认 `blake3_256` |
| `--queries` | 整数 | 证明中的查询次数 |
| `--blowup` | 2 的幂 | LDE blowup；Poseidon 和 SQL AIR 至少需要 8（默认值满足） |
| `--grinding` | 整数 | 查询种子的 grinding 因子 |
| `--field_extension` | `1` / `2` / `3` | 组合多项式的扩域次数 |
| `--folding` | 2 的幂 | FRI folding factor |
| `-n` | 整数 | 各子命令的规模参数 |

密度作用范围：

- `matrix-mul`：A、B 各 `size²` 个元素，独立采样；
- `poseidon`：`2 × hashes` 个输入；
- `sql`：每行 amount 列（共 `rows` 个），category 保持随机，不做 UINT32 分解；
- 非 `full` 值仅对上述三个示例有效，其他示例报错；`make stark-matmul` 可用 `DENSITY=sparse` 传入。

```bash
"$WINTERFELL" matrix-mul -n 8 --density sparse --profile
"$WINTERFELL" poseidon -n 2 --density hybrid --profile
"$WINTERFELL" sql -n 4 --density=sparse --profile
```

其中 `WINTERFELL=protocols/stark/winterfell/target/release/winterfell`。Poseidon 使用 f128 域参数（`x^5`、`t=3`、`R_F=8`、`R_P=57`，round constants 由 Grain LFSR 生成，MDS 为 Cauchy 矩阵）；SQL 使用 Poseidon 哈希链承诺，公开输入为 target、COUNT、SUM 和链摘要。

其他示例还包括 Fibonacci 变体、VDF、Rescue hash chain、随机化 AIR permutation、Merkle membership 和 Lamport+ signature 等，均不支持密度档位。

### 2. Setup & Prove

STARK 没有 Setup/SRS，也没有独立的电路文件：`winterfell <示例>` 在运行时构造轨迹并生成证明，因此“Setup & Prove”对应一次程序运行中的轨迹 + 证明阶段：

```bash
make stark THREADS=8
make stark-matmul THREADS=8 MATRIX_SIZE=8 DENSITY=sparse PROFILE=1

WINTERFELL=protocols/stark/winterfell/target/release/winterfell
RAYON_NUM_THREADS=8 "$WINTERFELL" matrix-mul -n 8 --density sparse --profile
RAYON_NUM_THREADS=8 "$WINTERFELL" poseidon -n 2 --density sparse --profile
RAYON_NUM_THREADS=8 "$WINTERFELL" sql -n 4 --density sparse --profile
```

- 并行度由 `RAYON_NUM_THREADS`（或 `make` 的 `THREADS`）控制；
- `--profile` 输出输入生成、轨迹构造、证明生成、验证等阶段耗时；
- 证明参数（`--hash_fn`、`--queries`、`--blowup`、`--grinding`、`--field_extension`、`--folding`）会影响证明时间、证明大小和安全级别。

### 3. Verify

验证与证明在同一次运行中完成：程序生成证明后先做序列化/反序列化往返，再调用验证器，并输出证明大小、安全级别、证明哈希和验证耗时；没有独立的 verify 程序。

```bash
WINTERFELL=protocols/stark/winterfell/target/release/winterfell
"$WINTERFELL" --help
"$WINTERFELL" poseidon -n 2 --density sparse
```

更详细的组件、AIR 实现和命令说明见 [protocols/stark/README.md](protocols/stark/README.md)。

## 分层规则

- `app/` 只负责解析命令和组织文件流程，入口实现直接放在该目录，不设置空的 `include/`、`src/` 或 `backends/` 子层级。
- `protocols/*/common` 负责公共协议类型、Transcript/I/O 规则、验证以及与后端无关的语义。
- `protocols/*/backends` 负责协议专属的加速数据流和融合内核。
- `operators/*` 负责与协议无关的域运算、曲线运算、NTT、MSM、内存管理、扫描、逐点求逆、多项式求值和综合除法等基础算子。
- 公共协议头文件不得暴露 `cudaStream_t`、`hipStream_t` 或设备指针。
- 一个协议不得包含另一个协议的后端头文件。
- `common/`、`operators/` 和 `protocols/` 下不保留空的占位目录；新增目录必须同时承载实际源码或头文件。

详细的依赖方向与迁移约束请参阅 [docs/architecture.md](docs/architecture.md)，导入源码的来源请参阅 [docs/provenance.md](docs/provenance.md)。

## 常用变量

- `JOBS`：构建并行度，默认为 `nproc`；
- `PROFILE=1`：启用支持的性能分析输出；
- `CUDA_ARCH`：NVIDIA 计算能力；
- `HIP_ARCH`：HIP offload target；
- `LIGHTZK_DEPS_DIR`：内置依赖根目录，默认为 `third_party/lightzk/depends`；
- `GROTH16_BUILD_DIR`：CMake 构建目录，默认为 `build/groth16`；
- `GROTH16_BIN_DIR`：可执行文件目录，默认为 `build/groth16/bin`；
- `GROTH16_DATA_DIR`：Groth16 产物目录，默认为 `build/groth16/data`。
- `PLONK_BUILD_DIR`：PLONK CMake 构建目录，默认为 `build/plonk`；
- `PLONK_BIN_DIR`：PLONK 可执行文件目录，默认为 `build/plonk/bin`；
- `PLONK_DATA_DIR`：PLONK 产物目录，默认为 `build/plonk/data`。
- `PLONK_CPU_MATRIX_SIZE`：`plonk-test-cpu` 使用的矩阵规模，默认为 `2`。
- `PLONK_CUDA_MATRIX_SIZE`：`plonk-test-cuda` 使用的矩阵规模，默认为 `10`。
- `PLONK_HIP_MATRIX_SIZE`：`plonk-test-hip` 使用的矩阵规模，默认为 `10`。
- `THREADS`：Winterfell/Rayon 工作线程数，默认为 `4`；
- `MATRIX_SIZE`：Winterfell 矩阵乘法示例规模，默认为 `4`，必须是不小于 2 的 2 的幂；
- `DENSITY`：Winterfell 矩阵乘法示例的输入密度，默认为 `full`，可选 `hybrid` / `sparse`。
