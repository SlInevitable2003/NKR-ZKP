# 内置的 LightZK 依赖

`depends/` 是 2026-10-12 从 `/home/slinevit2003/lightZK/depends` 复制的源码快照。它有意直接提交到 NKR-ZKP，而不是作为 Git 子模块引用。

该快照包含 NKR-ZKP 的 BN254 协议实现所共用的源码：

- libsnark 及所需的 libff/libfqfft 源码；
- NVIDIA CGBN 头文件；
- 来源工作区中原先尚未提交的本地修改。

其中，`libff/algebra/fields/bigint.tcc` 使用线程独立的 `std::mt19937_64` 生成器，从而避免 OpenMP 工作线程共用随机数生成器，也避免为每个 limb 重复构造 `std::random_device`。

导入时已删除所有嵌套的 `.git` 目录、Git 间接引用文件和 `.gitmodules` 文件。后续更新必须以源码形式复制，并在 NKR-ZKP 主仓库的 Git 历史中接受评审。

该快照针对框架固定使用的 ALT_BN128 配置进行了裁剪。上游 GoogleTest、演示程序、性能分析工具、教程、测试向量、ADSNARK/SUPERCOP 代码，以及旧版 BN128 ate-pairing/xbyak 后端均未内置。NKR-ZKP 的协议级集成测试是当前维护的测试入口。
