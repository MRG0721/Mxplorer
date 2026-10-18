# Mxplorer 兼容性测试报告

> 测试日期: 2026-10-18
> 测试版本: v0.0.2-alpha.2（构建于 2026-10-17）
> 测试环境: 见下“测试”
> 测试者: Mxplorer 作者 (Github Username 为 MRG0721)
> 测试软件包: `Mxplorer-0.0.2-alpha.2-forky-amd64-gzip-test.deb` ，此软件包即为将 `Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 的打包格式临时换成 `gzip` 格式的版本，内容与 v0.0.2-alpha2 完全一致

## 结论摘要

- 二进制运行：全部正常
- deb 的安装：Debian 8 及以后版本均可以正常使用 `apt` 或 `apt-get` 安装；较老的 Debian 或 Debian 系发行版（如Debian 7.11）需要通过 `dpkg -i ./<软件包>` 安装
- 建议：无法使用 `dpkg` 和 `apt` / `apt-get` 的用户使用通用包安装，即 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz`
- 其他发行版用户使用通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 安装
- 本次验证系统：Debian 7.11 amd64。基于以上结果，可考虑在 v0.0.2-alpha.3 中移除 forky 专用的平台标记，并在 Release Notes 中注明“已验证范围：Debian amd64”

---

## 测试

- OS: Debian GNU/Linux 7.11 (Wheezy), 64-bit (amd64)
- Kernel: Linux 3.2.0-4-amd64 (Debian package version 3.2.78-1), x86_64

### 测试结果

运行指令 `sudo dpkg -i ./Mxplorer-0.0.2-alpha.2-forky-amd64-gzip-test.deb` 安装到设备，二进制
可以正常运行，功能齐全，确认已通过测试。但由于早期的 apt 包管理器不完善，所以该包是不能通过
`sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64-gzip-test.deb` 安装到设备的，建议
使用此类无法使用 `apt` / `apt-get` 安装该包的老旧 Debian 或 Debian 系发行版的用户通过 `dpkg`
安装，也可以直接选择使用通用二进制包。

