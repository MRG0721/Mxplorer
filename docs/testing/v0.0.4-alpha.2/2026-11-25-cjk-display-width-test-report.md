# Mxplorer 0.0.4-alpha.2 CJK 终端宽度修正报告

> 测试日期：2026-11-25<br>
> 测试版本：`0.0.4-alpha.2`<br>
> 测试环境：项目开发机，详见 [test-platform.md](/docs/testing/test-platform.md)

## 结论

CLI 现在按终端单元格而不是 UTF-8 字节计算显示宽度，修正混合 ASCII/CJK
文件名的多列对齐、行编辑器光标移动、Tab 候选换行和搜索进度目录截断。

当前开发树为八套 ctest、343 项检查。本轮配置全部通过：

| 配置 | ctest | 结果 |
| --- | ---: | --- |
| GCC 16.2 Debug + `-Werror` | 8/8 | 通过 |
| GCC 16.2 ASan + UBSan + `-Werror` | 8/8 | 通过 |
| GCC 16.2 `-fanalyzer` + `-Werror` | 8/8 | 通过 |
| Alpine 3.24.0 / GCC 15.2.0 / musl static-pie | 8/8 | 通过 |

## 实现边界

- 新模块位于 `cli/src/terminal_text_width.*`，没有修改 `mxplorer_core`。
- 使用局部 `mbstate_t`、`mbrtowc()` 和 locale-aware `wcwidth()`。
- 没有全局缓存、ANSI 转义解析、第三方运行时依赖或 Qt 类型。
- Qt 前端将来应使用 `QFontMetrics` 和当前字体测量文本，不依赖这个终端工具。
- 非法 UTF-8 和不可打印字符采用安全回退；组合字符宽度为 0。
- ZWJ emoji、终端 Tab 停止位和任意控制字符的精确显示仍不在保证范围内。

## 回归覆盖

- `cli_text_width`：15 项，覆盖 ASCII、中文、日文、韩文、全角字符、
  组合字符、非法 UTF-8、嵌入 NUL、范围测量和左侧省略。
- `cli_smoke`：165 项，新增固定 80 列下混合 ASCII/CJK 完整布局断言。
- `cli_completion_pty`：8 项，新增真实 PTY 中文目录 Tab 补全和执行。
- 其余六套测试保持通过。
