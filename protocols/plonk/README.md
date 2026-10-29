# PLONK

本目录是基于 BN254 和 KZG 的 PLONK C++ 实现框架，其分层有意与 Groth16 保持一致：

- `app/`：与平台无关的 `gen/setup/prove/verify` 入口；
- `common/`：协议类型、公共 API、SHA3-256 Transcript、验证逻辑、后端选择，以及复用共享 `BinaryArchive` 的产物 I/O；
- `backends/cpu`：基于 libff/libfqfft 的参考后端；
- `backends/cuda` 和 `backends/hip`：建立在顶层公共算子之上的轻量协议适配层。

协议类型和函数位于 `nkr::plonk`，与 Groth16 的 `nkr::groth16` 对称。算术化构建器位于独立的 `Plonkish` 命名空间，对应 Groth16 电路生成阶段使用的 `LightZK` R1CS 构建器，不混入协议 API。`ProfileScope` 和 `configure_profiling` 不属于任何具体协议，位于顶层共享模块及 `nkr` 命名空间；两个协议使用协议前缀区分计时标签。

原有的 Rust CPU/CUDA 代码已删除。它们基于不同的软件栈，实现的是另一种带查找功能的四线协议，因此不会作为当前框架的实现基础。

当前已实现三线算术化 Manager、SHA3-256 Transcript、CPU Setup/Prove/Verify、CUDA Prove、基于共享 `BinaryArchive` 的产物序列化，以及完整的 `plonk-gen/setup/prove/verify` 命令行流程。低层协议文档见 `docs/plonk_protocol_low_level.pdf`。

## Plonkish 算术化 Manager

`common/include/plonkish/arithmetization.hpp` 提供独立的 `Plonkish::PlonkishManager`，不复用 Groth16 的 R1CS Manager，也不属于 `nkr::plonk` 协议接口。它直接描述经典三线 PLONK 门：

```text
qL·a + qR·b + qM·a·b + qO·c + qC + PI = 0
```

当前支持公共变量、私有变量、常量、加法、减法、乘法、线性组合、相等约束及底层算术门。`finalize()` 生成：

- 五个固定选择器的 H 点值；
- A、B、C 三列执行轨迹；
- 位于前若干行的公共输入列；
- 填充到 2 的幂后的计算域；
- 根据同一变量的所有线槽出现位置生成的三列复制置换。

算术化单元测试默认不参与常规构建，可以显式执行：

```bash
make plonk-test-arithmetization
```

## CPU Setup

`backends/cpu` 已实现经典三线 PLONK 的预处理阶段：

- 检查计算域、选择器、复制置换、SRS 次数上限和 SRS 幂次一致性；
- 自动生成 SRS 时，先构造 `1, tau, ..., tau^(N+2)`，再复用 G1 固定基窗口表并行生成全部幂次点；
- 选择互不相交的 `H`、`k1·H`、`k2·H`，并把线槽置换编码为三个置换多项式；
- 对五个选择器和三个置换多项式分别执行 N 点 iNTT；
- 使用单项式形式 SRS，以调用层分块并行的 BDLO12 MSM 完成八次 KZG 承诺；
- 在 `M = 4N` 的陪集域上缓存八个固定多项式的点值；
- 在 Proving Key 中缓存 Setup 生成的 `1, omega, ..., omega^(N-1)`，供 Prove 直接使用；
- 生成 N 点域和 M 点商域的 NTT/iNTT/coset 幂表并写入 Proving Key，避免 Prove 重复构造；
- 分别生成包含固定多项式数据的 Proving Key 和精简的 Verification Key。

CPU Setup 单元测试使用已知 `tau` 临时构造 SRS，仅用于验证承诺结果，不能用于生产环境：

```bash
make plonk-test-cpu-setup
```

## Transcript、CPU Prove 与 Verify

`common/include/plonk/transcript.hpp` 实现 SHA3-256 Fiat–Shamir Transcript。其状态更新参考 [Lambdaworks Transcript Strategy](https://lambdaclass.github.io/lambdaworks/plonk/implementation.html#transcript-strategy)：新消息追加到既有状态之前，`challenge()` 对当前状态求摘要，并用该摘要重置状态。初始化时先吸收域分离标签、N、验证密钥中的八个电路承诺和完整公开输入列，避免弱 Fiat–Shamir。

CPU Prove 按 PDF 的五轮流程实现 wire 盲化承诺、置换累积多项式、扩展陪集上的商多项式、随机点求值、线性化与两个 KZG 打开。Verify 重建同一 Transcript，检查商多项式标量恒等式，并验证 `zeta` 与 `zeta·omega` 两个 KZG pairing 等式。负向测试覆盖被修改的求值、打开证明、公开输入和验证密钥：

```bash
make plonk-test-cpu-prove-verify
```

Setup 的调用层并行度由 `OMP_NUM_THREADS` 控制，但不启用 libff/libfqfft 的全局 `MULTICORE` 宏。CPU Prove 作为比较基准保持单线程。使用 `--profile` 时，CPU Setup 会分别报告 SRS 标量幂、固定基窗口表、FixMSM、域准备、置换构造，以及每个固定多项式的 iNTT、MSM 和 coset NTT；CPU Prove 会报告五轮总耗时，并进一步细分 wire/quotient/opening MSM、batch inversion、grand product、coset NTT、商多项式逐点评估、inverse coset NTT、线性化和 synthetic division。

## CUDA Prove

`backends/cuda` 与 Groth16 CUDA 后端采用相同的组织方式：协议后端只编排五轮数据流，BN254 域/曲线、NTT、MSM、scan、逐点求逆、多项式求值和综合除法位于顶层 `operators/cuda`。`plonk-prove` 先执行不计入在线证明时间的 prepare 阶段，把固定多项式的系数、H 点值、商域点值、SRS bases 以及 Setup 生成的 NTT 幂表一次上传并在整个证明期间驻留显存；正式 Prove 只上传 witness 和公开输入，轮次之间不往返传输多项式。Transcript 与挑战生成留在 CPU，MSM 超出 N 项的至多三个尾系数也按既定算子边界在 CPU 合并。

逐点求逆直接复用 `mont_t.cuh` 中由相邻线程协作的 `reciprocal()`；置换累积乘积和综合除法复用同一个支持非交换结合操作的递归 scan。小规模和跨 block 路径由 `plonk-cuda-operators-test` 与 CPU 结果对照，覆盖 NTT/coset NTT、MSM、求逆、乘积 scan、多项式求值和综合除法。

CUDA Setup 当前有意委托 CPU Setup，不在 GPU 上生成 SRS；NTT 幂表同样由 CPU Setup 并行生成并随 PK 保存。CUDA prepare 使用 `plonk.cuda.prepare` 前缀单独计时；正式 Prove 的 profile 标签与 CPU 保持相同的五轮和子阶段层次，前缀为 `plonk.cuda.prove`。`plonk.prove.total` 只覆盖正式证明，包含文件读写、校验、prepare 和释放资源的端到端时间由 `plonk.prove.program_total` 报告。

## 产物序列化与命令行

`common/src/io.cpp` 提供 `circuit`、`witness`、`srs`、`proving-key`、`verification-key`、`proof` 的读写，复用共享的 `BinaryArchive`（见 `common/include/binary_archive.hpp`）。文件以魔数 `LKZ1` 和版本号开头，向量带字节长度前缀；元素按原始内存布局读写，因此要求读写程序使用兼容的 ABI、依赖版本与构建配置。该格式用于本项目程序之间传递产物，不承诺规范化的跨实现编码。

与 Groth16 一样，PLONK 始终使用同名的四个统一程序，不为不同后端分别生成可执行文件。CPU 构建支持 `--backend cpu`；CUDA 构建同时支持 `--backend cpu` 和 `--backend cuda`。HIP 协议适配仍是框架，暂不支持 `--backend hip`。`plonk-gen` 和 `plonk-verify` 不接收 `--backend`。

`plonk-gen` 目前支持 `matmul` 与 `fibonacci` 两种用例；`plonk-setup` 在未提供 `--srs` 时现场生成并写出 SRS。下面给出两种电路的完整 CPU 命令，其中第二次 Setup 复用第一次生成的 SRS：

```bash
make plonk-cpu

PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" matmul 2
"$PLONK_BIN/plonk-setup" --backend cpu matmul_2.bin
"$PLONK_BIN/plonk-prove" --backend cpu matmul_2.bin
"$PLONK_BIN/plonk-verify" matmul_2.bin

"$PLONK_BIN/plonk-gen" fibonacci 10
"$PLONK_BIN/plonk-setup" --backend cpu \
  --srs matmul_2.srs.bin fibonacci_10.bin
"$PLONK_BIN/plonk-prove" --backend cpu fibonacci_10.bin
"$PLONK_BIN/plonk-verify" fibonacci_10.bin
```

CUDA 的完整构建与运行命令如下；RTX 5060 的计算能力为 `120`：

```bash
make plonk-cuda CUDA_ARCH=120

PLONK_BIN=build/plonk/bin
"$PLONK_BIN/plonk-gen" matmul 10
"$PLONK_BIN/plonk-setup" --backend cuda matmul_10.bin
"$PLONK_BIN/plonk-prove" --backend cuda matmul_10.bin
"$PLONK_BIN/plonk-verify" matmul_10.bin
```

包含共享算子对照测试和端到端证明验证的快速回归命令为：

```bash
make plonk-test-cuda CUDA_ARCH=120 PLONK_CUDA_MATRIX_SIZE=10
```

上述命令使用独立的 `build/plonk` 构建树，默认产物目录为 `build/plonk/data`。与 Groth16 相同，`setup`、`prove` 和 `verify` 可用 `--path <dir>` 覆盖该目录；使用默认值时无需传入。自动生成 SRS 使用 libff 的 `random_element()`，适合本地开发和功能验证；它不是多方可信设置，不能直接作为生产环境的通用 SRS。CPU 快速回归可执行 `make plonk-test-cpu`。
