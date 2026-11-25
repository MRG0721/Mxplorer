# Mxplorer 0.0.4-alpha.1 长选项解析器加固报告

> 测试日期：2026-11-25<br>
> 测试基线：`0.0.4-alpha.1` / `fa6ed4d`<br>
> 测试范围：测试与文档加固；没有改变 CLI 运行时行为<br>
> 测试环境：项目开发机，详见 [test-platform.md](/docs/testing/test-platform.md)

## 结论

新增长选项解析器的独立测试目标 `cli_options`，直接检查 44 项解析语法和错误
数据。当前开发树的 ctest 从六套扩展为七套，检查总数从 280 项增加到 324 项。

本轮配置全部通过：

| 配置 | ctest | 结果 |
| --- | ---: | --- |
| GCC 16.2 Debug + `-Werror` | 7/7 | 通过 |
| GCC 16.2 ASan + UBSan + `-Werror` | 7/7 | 通过 |
| GCC 16.2 `-fanalyzer` + `-Werror` | 7/7 | 通过 |
| Alpine 3.24.0 / GCC 15.2.0 / musl static-pie | 7/7 | 通过 |

ASan/UBSan 运行时关闭了本环境不可用的 LeakSanitizer，其他 sanitizer 检查正常启用。

## 直接覆盖的解析边界

- `--all`、`--long` 等长布尔选项；
- `-al` 等组合短选项及顺序保留；
- `--type=value` 与 `--type value`；
- `-tf`、`-d2` 等短选项附加值；
- `--size -10M` 等以横线开头的值；
- `--` 终止符及以横线开头的路径；
- 单个 `-`、空字符串和普通位置参数；
- 未知长/短选项、布尔选项错误使用 `=value`；
- 缺失长/短值；
- 重复值采用最后一次、重复布尔标记只记录一次；
- 长选项大小写敏感；
- `find` 与 `search` 共用规格；
- `tree --level`；
- 显式空值 `--content=`。

## 范围说明

本轮只增加测试与文档，确保后续开发可以更可靠地修改 CLI 选项语法。未执行新的
Debian 兼容性矩阵。
