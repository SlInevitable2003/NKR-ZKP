# PLONK

本目录是基于 BN254 和 KZG 的 PLONK C++ 实现框架，其分层有意与 Groth16 保持一致：

- `app/`：与平台无关的 `gen/setup/prove/verify` 入口；
- `common/`：协议类型、公共 API、后端选择，以及未来的 Transcript、规范化 I/O、KZG 和验证逻辑；
- `backends/cpu`：基于 libff/libfqfft 的参考后端；
- `backends/cuda` 和 `backends/hip`：建立在顶层公共算子之上的轻量协议适配层。

原有的 Rust CPU/CUDA 代码已删除。它们基于不同的软件栈，实现的是另一种带查找功能的四线协议，因此不会作为当前框架的实现基础。

当前目标可以编译，但会明确输出“仅为框架”的提示并以状态码 2 退出。现阶段尚未实现任何 PLONK 证明语义。
