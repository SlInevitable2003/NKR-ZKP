"""CSV/JSON/Markdown reporting."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .metrics import write_cases_csv
from .util import ensure_dir, read_json, write_json


def load_results(run_dir: Path) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for path in sorted((run_dir / "cases").glob("*.json")):
        data = read_json(path, default=None)
        if isinstance(data, dict):
            results.append(data)
    return results


def _fmt(value: Any, digits: int = 3) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def _table(headers: list[str], rows: list[list[Any]]) -> str:
    lines = ["| " + " | ".join(headers) + " |"]
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        lines.append("| " + " | ".join(_fmt(cell) for cell in row) + " |")
    return "\n".join(lines)


def write_report(run_dir: Path) -> dict[str, Any]:
    ensure_dir(run_dir)
    results = load_results(run_dir)
    results.sort(key=lambda item: (item.get("suite", ""), item.get("case_id", "")))
    summary_csv = run_dir / "summary.csv"
    write_cases_csv(summary_csv, results)
    write_json(run_dir / "report.json", {"cases": results})

    functional = [item for item in results if item.get("suite") == "functional"]
    p1 = [item for item in results if item.get("suite") == "p1"]
    p2 = [item for item in results if item.get("suite") == "p2"]
    p3 = [item for item in results if item.get("suite") == "p3"]
    # Formal tables contain only cases that passed the qualification gate.
    # Non-ok cases are listed in the appendix, not silently dropped.
    functional_ok = [item for item in functional if item.get("status") == "ok"]
    p1_ok = [item for item in p1 if item.get("status") == "ok"]
    p2_ok = [item for item in p2 if item.get("status") == "ok"]
    p3_ok = [item for item in p3 if item.get("status") == "ok"]

    lines: list[str] = []
    lines.append("# NKR-ZKP 测试报告")
    lines.append("")
    environment = read_json(run_dir / "environment.json", default={})
    if environment:
        lines.append("## 测试环境")
        lines.append("")
        for key, value in environment.items():
            lines.append(f"- `{key}`: {value}")
        lines.append("")

    lines.append("## 功能性测试")
    lines.append("")
    if functional_ok:
        rows = [
            [
                item.get("protocol"),
                item.get("circuit"),
                item.get("display_platform"),
                item.get("t_max_ms"),
                item.get("throughput_proofs_s"),
                item.get("status"),
            ]
            for item in functional_ok
        ]
        lines.append(
            _table(
                ["协议", "电路", "平台/标签", "T_max(ms)", "吞吐(proof/s)", "状态"],
                rows,
            )
        )
    else:
        lines.append("无功能性测试结果。")
    lines.append("")

    lines.append("## P1 单卡 / P2 多卡")
    lines.append("")
    for title, entries in (("P1 单卡", p1_ok), ("P2 多卡", p2_ok)):
        lines.append(f"### {title}")
        lines.append("")
        if not entries:
            lines.append("无结果。")
            lines.append("")
            continue
        rows = [
            [
                item.get("circuit"),
                item.get("density"),
                item.get("k"),
                item.get("display_platform"),
                item.get("devices"),
                item.get("concurrency"),
                item.get("t_max_ms"),
                item.get("throughput_proofs_s"),
                item.get("speedup"),
                item.get("util_mean"),
                item.get("status"),
            ]
            for item in entries
        ]
        lines.append(
            _table(
                [
                    "电路",
                    "密度",
                    "k",
                    "平台",
                    "卡数",
                    "并发",
                    "T_max(ms)",
                    "吞吐(proof/s)",
                    "加速比",
                    "利用率(%)",
                    "状态",
                ],
                rows,
            )
        )
        lines.append("")

    lines.append("## P3 多机多卡")
    lines.append("")
    if p3_ok:
        rows = [
            [
                item.get("circuit"),
                item.get("density"),
                item.get("proof_bits"),
                item.get("equivalent_bits"),
                item.get("nodes"),
                item.get("devices"),
                item.get("waves"),
                item.get("t_max_ms"),
                item.get("throughput_equiv_2_30_s"),
                item.get("speedup"),
                item.get("util_mean"),
                item.get("status"),
            ]
            for item in p3_ok
        ]
        lines.append(
            _table(
                [
                    "电路",
                    "密度",
                    "proof k",
                    "等价 k",
                    "节点",
                    "每节点卡数",
                    "wave",
                    "T_total(ms)",
                    "等价 2^30/s",
                    "加速比",
                    "利用率(%)",
                    "状态",
                ],
                rows,
            )
        )
    else:
        lines.append("无 P3 结果。")
    lines.append("")

    qualification = read_json(run_dir / "qualification.json", default={})
    if isinstance(qualification, dict) and qualification:
        qualified = sum(
            1
            for value in qualification.values()
            if isinstance(value, dict) and value.get("qualified")
        )
        lines.append("## 90% 资格测试")
        lines.append("")
        lines.append(
            f"- 已标定配置：{len(qualification)}；通过：{qualified}；"
            f"未通过：{len(qualification) - qualified}"
        )
        unqualified = [
            key
            for key, value in qualification.items()
            if not (isinstance(value, dict) and value.get("qualified"))
        ]
        if unqualified:
            lines.append("- 未通过配置（原始数据保留在该 run 目录）：")
            for key in unqualified:
                entry = qualification[key]
                if not isinstance(entry, dict):
                    entry = {}
                lines.append(
                    f"  - `{key}`: util_mean={_fmt(entry.get('util_mean'))}, "
                    f"p={entry.get('concurrency')}"
                )
        lines.append("")

    non_ok = [item for item in results if item.get("status") != "ok"]
    if non_ok:
        lines.append("## 异常与未达标附录")
        lines.append("")
        lines.append("以下配置未进入正式结论表，原始数据保留在 run 目录。")
        lines.append("")
        rows = [
            [
                item.get("case_id"),
                item.get("status"),
                item.get("util_mean"),
                item.get("note"),
            ]
            for item in non_ok
        ]
        lines.append(_table(["case", "状态", "利用率(%)", "备注"], rows))
        lines.append("")

    lines.append("## 说明")
    lines.append("")
    lines.append(
        "- `T_max` 为同一测量内所有 Prove 的最大在线时间；吞吐 = 证明数 / T_max。"
    )
    lines.append(
        "- P3 每卡跑 16 个 `2^26` 证明，等价于每卡一个 `2^30` 证明；"
        "`T_total` 为 16 个 wave 的全局最大时间之和。"
    )
    lines.append(
        "- `GPU-sim` / `DCU-sim` 是 STARK 的 CPU 线程标签（4 线程 / 3 线程），"
        "不是真实 GPU/DCU 结果；STARK 目前只有 CPU 实现。"
    )
    lines.append(
        "- 利用率低于 90% 的配置状态为 `util_unqualified`，不进入正式结论；"
        "原始数据保留。"
    )
    lines.append("")

    (run_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    return {"cases": len(results), "report": str(run_dir / "report.md")}
