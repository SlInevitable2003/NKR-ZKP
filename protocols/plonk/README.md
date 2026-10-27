# PLONK

本目录是基于 BN254 和 KZG 的 PLONK C++ 实现框架，其分层有意与 Groth16 保持一致：

- `app/`：与平台无关的 `gen/setup/prove/verify` 入口；
- `common/`：协议类型、公共 API、后端选择，以及未来的 Transcript、规范化 I/O、KZG 和验证逻辑；
- `backends/cpu`：基于 libff/libfqfft 的参考后端；
- `backends/cuda` 和 `backends/hip`：建立在顶层公共算子之上的轻量协议适配层。

原有的 Rust CPU/CUDA 代码已删除。它们基于不同的软件栈，实现的是另一种带查找功能的四线协议，因此不会作为当前框架的实现基础。

当前命令行程序可以编译，但仍会明确输出“仅为框架”的提示并以状态码 2 退出。现阶段尚未实现 Setup、Prove 和 Verify。

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

CPU Setup、产物序列化以及 `plonk-gen` 的文件输出仍待实现。
