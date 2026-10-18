# Mxplorer 兼容性测试报告

> 测试日期: 2026-10-17
> 测试版本: v0.0.2-alpha.2（构建于 2026-10-17）
> 测试环境: 见下列多轮测试
> 测试者: Mxplorer 作者 (Github Username 为 MRG0721)
> 测试软件包: `Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 和 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz`

## 结论摘要

- 二进制运行：全部正常
- deb 的安装：Debian 8 及以后版本均可以正常使用 `apt` 或 `apt-get` 安装；Debian 7.11 因旧版 `dpkg` 对 `.tar.xz` 支持不完善，无法通过 `apt-get` 安装
- 建议：使用较老旧 Debian 系发行版，或是 dpkg 版本过低的用户使用通用包安装，即 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz`
- 其他发行版用户使用通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 安装
- 本次验证范围：Debian 7.11 - Debian 13.7，以及滚动版 forky/sid，均为 amd64。基于以上结果，可考虑在 v0.0.2-alpha.3 中移除 forky 专用的平台标记，并在 Release Notes 中注明“已验证范围：Debian amd64”

---

## 测试1

- OS: Debian GNU/Linux 7.11 (Wheezy), 64-bit (amd64)
- Kernel: Linux 3.2.0-4-amd64 (Debian package version 3.2.78-1), x86_64

### 测试结果

经人工测试，Mxplorer 的二进制可以正常运行，功能齐全，但由于 Debian 7.11 所使用的旧版 `dpkg`
对 `Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 包里使用的 `.tar.xz` 格式支持不完善，所以无法正常
使用 `sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 指令完成安装。
但二进制运行的测试通过，各功能正常。对于这一类无法正常使用 `apt-get` 安装的发行版，推荐
通过通用二进制压缩包安装，即 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz`，该包
在本轮测试中可用。

---

## 测试2

- OS: Debian GNU/Linux 8.11 (Jessie), 64-bit (amd64)
- Kernel: Linux 3.16.0-6-amd64 (Debian package version 3.16.56-1+deb8u1, 2018-05-08), x86_64

### 测试结果

该操作系统可以用 `sudo apt install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 或
`sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 正常完成对
`Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 的安装，在对其二进制的运行的测试中，一切正常，未
出现 bug，测试通过，各功能正常。
通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 经测试同样过关，测试通过。

---

## 测试3

- OS: Debian GNU/Linux 9.13 (Stretch), 64-bit (amd64)
- Kernel: Linux 4.9.0-13-amd64 (Debian package version 4.9.228-1, 2020-07-05), x86_64

### 测试结果

该操作系统可以用 `sudo apt install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 或
`sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 正常完成对
`Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 的安装，在对其二进制的运行的测试中，一切正常，未
出现 bug，测试通过，各功能正常。
通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 经测试同样过关，测试通过。

---

## 测试4

- OS: Debian GNU/Linux 11.11 (Bullseye), 64-bit (amd64)
- Kernel: Linux 5.10.0-32-amd64 (Debian package version 5.10.223-1, 2024-08-10), x86_64

### 测试结果

该操作系统可以用 `sudo apt install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 或
`sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 正常完成对
`Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 的安装，在对其二进制的运行的测试中，一切正常，未
出现 bug，测试通过，各功能正常。
通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 经测试同样过关，测试通过。

---

## 测试5

- OS: Debian GNU/Linux 13.7 (Trixie), 64-bit (amd64)
- Kernel: Linux 6.12.107+deb13-amd64 (Debian package version 6.12.107-1, 2026-08-29), x86_64

### 测试结果

该操作系统可以用 `sudo apt install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 或
`sudo apt-get install ./Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 正常完成对
`Mxplorer-0.0.2-alpha.2-forky-amd64.deb` 的安装，在对其二进制的运行的测试中，一切正常，未
出现 bug，测试通过，各功能正常。
通用包 `Mxplorer-0.0.2-alpha.2-forky-linux-amd64-static.tar.gz` 经测试同样过关，测试通过。

---

## 开发环境 (滚动更新，非固定版本)

- OS: Debian GNU/Linux forky/sid, 64-bit (amd64)
- Kernel: Linux 7.2.9+deb14-amd64 (Debian package version 7.2.9-1, 2026-10-03), x86_64

### 测试结果

本机开发、本机编译、本机完美运行。