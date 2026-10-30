# Mxplorer 兼容性测试报告

> 测试日期: 2026-10-20
> 测试版本: v0.0.3-alpha.1（构建于 2026-10-19）
> 测试环境: 使用 [test-platform.md](/docs/testing/test-platform.md) 列出的平台
> 测试者: Mxplorer 作者 (Github Username 为 MRG0721)
> 测试软件包: `Mxplorer-0.0.3-alpha.1-amd64.deb` 和 `Mxplorer-0.0.3-alpha.1-linux-amd64-static.tar.gz`

## 结论摘要

- 二进制运行：全部正常，v0.0.3-alpha.1 加入的补全功能完整可用，未发现bug
- deb 的安装：Debian 8 及以后版本均可以正常使用 `apt` 或 `apt-get` 安装；较老的 Debian 或 Debian 系发行版（如Debian 7.11）需要通过 `sudo dpkg -i ./Mxplorer-0.0.3-alpha.1-amd64.deb` 安装
- 建议：无法使用 `dpkg` 和 `apt` / `apt-get` 的用户使用通用包安装，即 `Mxplorer-0.0.3-alpha.1-linux-amd64-static.tar.gz`
- 其他发行版用户使用通用包 `Mxplorer-0.0.3-alpha.1-linux-amd64-static.tar.gz` 安装
- 本次验证范围：Debian 7.11 - Debian 13.7，以及滚动版 forky/sid，均为 amd64。基于以上结果，现决定发布 v0.0.3-alpha.2 作为 Verified Compatibility

---

## 测试详情

| 平台 | 测试项目 | 结果 |
| --- | --- | --- |
| Debian 7.11 | 安装、兼容性、二进制有无bug | 需使用 `dpkg -i` 安装，均通过 |
| Debian 8.11 | 安装、兼容性、二进制有无bug | 通过 |
| Debian 9.13 | 安装、兼容性、二进制有无bug | 通过 |
| Debian 11.11 | 安装、兼容性、二进制有无bug | 通过 |
| Debian 13.7 | 安装、兼容性、二进制有无bug | 通过 |
| 开发环境 | [Comprehensive Test](/docs/testing/v0.0.3-alpha.1/2026-10-19-v0.0.3-alpha.1-comprehensive-test-report.md) | 通过 |

### 开发环境 @ 2026-10-20

- OS: Debian GNU/Linux forky/sid, 64-bit (amd64)
- Kernel: Linux 7.2.11+deb14-amd64 (Debian package version 7.2.11-1, 2026-10-15), x86_64
- 滚动更新，非固定版本
- 每日均会 `full-upgrade` 一次，最近的更新发生在 2026-10-20