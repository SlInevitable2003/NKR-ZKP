# PLONK

本目录是基于 BN254 和 KZG 的 PLONK C++ 实现框架，其分层有意与 Groth16 保持一致：

- `app/`：与平台无关的 `gen/setup/prove/verify` 入口；
- `common/`：协议类型、公共 API、后端选择、产物 I/O（复用共享 `BinaryArchive`），以及未来的 Transcript、KZG 打开和验证逻辑；
- `backends/cpu`：基于 libff/libfqfft 的参考后端；
- `backends/cuda` 和 `backends/hip`：建立在顶层公共算子之上的轻量协议适配层。

原有的 Rust CPU/CUDA 代码已删除。它们基于不同的软件栈，实现的是另一种带查找功能的四线协议，因此不会作为当前框架的实现基础。

当前已实现三线算术化 Manager、CPU Setup、基于共享 `BinaryArchive` 的产物序列化，以及 `plonk-gen` 与 `plonk-setup` 两个命令行程序。Prove 和 Verify 尚未实现，`plonk-prove` 与 `plonk-verify` 仍以状态码 2 退出。低层协议文档见 `docs/plonk_protocol_low_level.pdf`。

## Plonkish 算术化 Manager

`common/include/plonk/arithmetization.hpp` 提供独立的 `PlonkishManager`，不复用 Groth16 的 R1CS Manager。它直接描述经典三线 PLONK 门：

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
- 选择互不相交的 `H`、`k1·H`、`k2·H`，并把线槽置换编码为三个置换多项式；
- 对五个选择器和三个置换多项式分别执行 N 点 iNTT；
- 使用单项式形式 SRS 完成八次 KZG MSM 承诺；
- 在 `M = next_power_of_two(3N + 6)` 的陪集域上缓存八个固定多项式的点值；
- 分别生成包含固定多项式数据的 Proving Key 和精简的 Verification Key。

CPU Setup 单元测试使用已知 `tau` 临时构造 SRS，仅用于验证承诺结果，不能用于生产环境：

```bash
make plonk-test-cpu-setup
```

## 产物序列化与命令行

`common/src/io.cpp` 提供 `circuit`、`witness`、`srs`、`proving-key`、`verification-key`、`proof` 的读写，复用共享的 `BinaryArchive`（见 `common/include/binary_archive.hpp`）。文件以魔数 `LKZ1` 和版本号开头，向量带字节长度前缀；元素按原始内存布局读写，因此要求读写程序使用兼容的 ABI、依赖版本与构建配置。该格式用于本项目程序之间传递产物，不承诺规范化的跨实现编码。

`plonk-gen` 目前支持 `matmul` 与 `fibonacci` 两种用例；`plonk-setup` 在未提供 `--srs` 时现场生成并写出 SRS。下面给出两种电路的完整 CPU 命令，其中第二次 Setup 复用第一次生成的 SRS：

```bash
make plonk-cpu

PLONK_BIN=build/groth16/bin
PLONK_DATA=build/groth16/plonk-data

"$PLONK_BIN/plonk-gen" --path "$PLONK_DATA" matmul 2
"$PLONK_BIN/plonk-setup" --backend cpu --path "$PLONK_DATA" matmul_2.bin

"$PLONK_BIN/plonk-gen" --path "$PLONK_DATA" fibonacci 10
"$PLONK_BIN/plonk-setup" --backend cpu --path "$PLONK_DATA" \
  --srs matmul_2.srs.bin fibonacci_10.bin
```

自动生成 SRS 使用 libff 的 `random_element()`，适合本地开发和功能验证；它不是多方可信设置，不能直接作为生产环境的通用 SRS。快速回归仍可执行 `make plonk-test-cpu`。
