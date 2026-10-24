# Mxplorer
**[English](README.md) | [简体中文](README.zh-CN.md)**

一个跑在 Linux 上的简单的文件资源管理器。当前界面是终端里交互式 shell，
所有文件系统逻辑集中在一个不依赖任何 UI 的静态库 `mxplorer_core` 里，
将来的 Qt6 前端只是再加一个可执行文件，不碰 core。

名字来自作者的 Github 用户名 (username)，即 MRG0721 的首字母与 Explorer 的结合。
其写作 Mxplorer，而命令名、包名、命名空间与头文件目录则是一律小写 `mxplorer`
（类似 Git 与 `git`）。

项目还在开发中，1.0 之前不做兼容性承诺；`build/`、`dist/` 这类产物随时可以删掉
重新生成。

## 构建

构建是全静态的。编译器放在一个固定版本的 Alpine 3.24.0 rootfs 里
（GCC 15、musl 1.2），通过 bubblewrap 进入，缓存在 `build/musl/` 下，
不会在宿主机上安装任何东西。宿主机只需要 `bubblewrap`、`curl`、`tar`、
`sha256sum`、CMake（为了 `ctest`）、binutils（`readelf`、`objdump`）和 `file`。
第一次运行需要联网下载 rootfs 与工具链包，之后使用缓存，可以离线构建。

```sh
./packaging/build_static.sh          # 构建、静态检查、ctest
./build/musl/build/cli/mxplorer      # static-pie 二进制
./packaging/build_static.sh --rebuild-toolchain   # 重建工具链缓存
```

二进制把 musl、libstdc++ 与 libgcc 一起装进自身，运行时没有任何动态库依赖。
`ctest` 会在静态二进制上运行 `core_operations`、`core_utils`、`core_search`、
`cli_completion`、`cli_completion_pty` 与 `cli_smoke`。完整的
[0.0.3-alpha.1 测试报告](docs/testing/v0.0.3-alpha.1/2026-10-19-v0.0.3-alpha.1-comprehensive-test-report.md)
位于 `docs/testing/`。

除标准库外只用到几个 POSIX 接口：core 用 `pwd.h`（取家目录）、`fnmatch.h`（检索的
通配符匹配）、`sys/stat.h`（属性与时间戳）、`sys/xattr.h`（扩展属性与 ACL）以及少量
POSIX I/O 调用（`open` / `read` / `write` / `lseek` / `ftruncate` / `utimensat`）；
前端用 `unistd.h` / `sys/ioctl.h`（终端判断与宽度）和 `signal.h`。

## 目录结构

```
mxplorer/
├── CMakeLists.txt
├── .clang-format             # Allman 花括号风格，4 空格 / 100 列
├── LICENSE                   # GPL-3.0-or-later，未改动的 GPLv3 全文
├── core/                     # mxplorer_core：没有 Qt、没有打印、没有 exit()、不抛异常
│   ├── include/mxplorer/
│   │   ├── callback.hpp      # CancelToken：界面无关的"请求停止"回调
│   │   ├── error.hpp         # ErrorCode / Error / Result<T>
│   │   ├── entry.hpp         # FileEntry：一个条目的全部数据
│   │   ├── directory.hpp     # list_directory / stat_path，排序与自然序比较
│   │   ├── operations.hpp    # copy / move / remove / mkdir + 进度回调 + 取消令牌
│   │   ├── search.hpp        # 按名字检索：glob/子串/正则 + 过滤 + 进度 + 取消
│   │   ├── path_utils.hpp    # UTF-8 转换、~ 展开、~/ 显示
│   │   └── session.hpp       # 当前目录状态、把用户输入解析成绝对路径
│   └── src/
├── cli/                      # 终端前端：解析命令行、打印结果、渲染进度
│   └── src/{main,shell,completion,line_editor}.cpp
├── packaging/                # 静态构建 + deb 打包
│   ├── build_static.sh       # 固定 Alpine/musl 工具链、构建与 ctest
│   └── build_deb.sh          # deb 与静态 tar.gz
└── tests/                    # 六套测试，都由 ctest 驱动
    ├── smoke_test.sh         # 终端前端的端到端检查
    ├── completion_test.cpp   # 命令、路径与选项补全
    ├── completion_pty_test.cpp # 通过伪终端测试真实 Tab 键
    ├── core_search_test.cpp  # 确定性的检索取消检查
    ├── core_operations_test.cpp  # core 的定点检查（复制原子性）
    └── core_utils_test.cpp   # 纯函数检查：折叠、路径、排序、错误
```

## 用法

交互模式（提示符显示当前目录，`cd` 状态会保持）：

```
$ ./build/musl/build/cli/mxplorer
mxplorer 0.0.3-alpha.1 | type 'help' for commands, Tab completes, 'exit' to quit
mxplorer:~$ cd mxplorer
mxplorer:~/mxplorer$ ls
...
```

非交互模式一次只执行一条命令，进程结束状态就是命令的结果（可直接用在脚本里）：

```sh
./build/musl/build/cli/mxplorer tree . -L 2
./build/musl/build/cli/mxplorer cp -r src backup/ && echo done
```

| 命令 | 说明 |
| --- | --- |
| `ls [-a] [-l] [path]` / `ll` | 列目录。`-a` 含隐藏项，`-l` 长格式；目录以 `/` 标记、符号链接以 `@` 标记 |
| `cd [path]` | 切换目录，`cd` 回家目录，`cd -` 回上一个目录 |
| `pwd` | 打印当前目录 |
| `cp [-r] [-f] [-p] src... dst` | 复制。`dst` 是已存在的目录时复制到其内部（同 cp(1)）；符号链接被重建而不跟随。`-p` 保留权限、时间戳、硬链接与扩展属性（ACL），稀疏文件保持稀疏 |
| `mv [-f] [-p] src... dst` | 移动/改名。同一文件系统走 `rename(2)`，跨设备自动退化为复制+删除 |
| `rm [-r] [-f] path...` | 删除。目录需要 `-r`；`-f` 让不存在的路径静默通过 |
| `mkdir [-p] path...` | 建目录，`-p` 建父目录且对已存在目录静默 |
| `stat path...` | 显示类型、大小、mtime、权限位 |
| `tree [path] [-L depth]` | 打印目录树 |
| `find <pattern> [path...] [选项]` / `search` | 按名字或内容检索，结果逐行打印绝对路径；`-i` 忽略大小写、`-E` 正则、`-F` 字面、`-l` 长格式、`-a` 含隐藏、`-t f\|d\|l` 类型、`-d N` 深度、`-n N` 上限、`-c text` 内容、`-s ±N[KMG]` 大小、`-m ±N[dhms]` 时间 |
| `help` / `clear` / `exit` / `quit` | — |

路径支持 `~`、`~user`、`~`/相对路径、`-`，以及 `'单引号'`、`"双引号"`、反斜杠转义
（`cd "my dir"` 可以正常工作）。

交互提示符里，Tab 可以补全命令名、文件系统路径和短选项。路径补全会保留 `~`、
目录分隔符并转义空格，所以 `cd my<Tab>` 会得到 `cd my\ dir/`。目录候选带尾斜杠；
若公共前缀无法继续扩展，再按一次 Tab 会列出候选。行编辑器还支持左右方向键、
Home/End、退格、Delete、Ctrl-A/E/B/F/U/K 与 Ctrl-W。目前没有持久化历史记录；
通过管道或其他非终端方式输入时仍保持原来的逐行行为。

## 打包成 deb

```sh
./packaging/build_deb.sh
# ==> dist/Mxplorer-0.0.3-alpha.1-amd64.deb
# ==> dist/Mxplorer-0.0.3-alpha.1-linux-amd64-static.tar.gz
```

| 系统 | 安装方式 |
| --- | --- |
| Debian 8+ | `sudo apt-get install ./dist/Mxplorer-0.0.3-alpha.1-amd64.deb` |
| Debian 7.11（旧 apt） | `sudo dpkg -i ./dist/Mxplorer-0.0.3-alpha.1-amd64.deb` |
| 其他发行版 | 解包 `Mxplorer-0.0.3-alpha.1-linux-amd64-static.tar.gz`，运行 `usr/bin/mxplorer` |

0.0.3-alpha.1 加入终端补全与行编辑，已
[作为 GitHub pre-release 发布](https://github.com/MRG0721/Mxplorer/releases/tag/v0.0.3-alpha.1)，
继续使用 0.0.2-alpha.3 已验证的全静态 musl 构建和通用产物命名。Debian 7 的旧 apt
不支持本地路径安装，因此使用 `dpkg -i`。
Debian 兼容性矩阵针对 0.0.2-alpha.3 验证；本次更新尚未重新跑整套发行版矩阵。

`build_deb.sh` 会先调用 `build_static.sh`（所以测试一定会跑），复制并 strip
二进制，把安装树铺在 `build/musl/stage/` 下，写好 control、md5sums、copyright、
changelog，最后交给 `dpkg-deb --build` 成包。deb 用 gzip 压缩
（`dpkg-deb -Zgzip`），以兼容 Debian 7 的旧 dpkg。deb **刻意没有 `Depends` 字段**：
二进制是全静态的，能装在任何 dpkg 系统上，与 glibc/libstdc++ 版本无关。
tar.gz 是同一棵安装树，给不用 dpkg 的系统。

包内容：

```
/usr/bin/mxplorer
/usr/share/man/man1/mxplorer.1.gz
/usr/share/doc/mxplorer/{copyright,changelog.gz}
```

注意：

- 维护者署名是 `MRG0721 <271227737+MRG0721@users.noreply.github.com>`：提交作者、
  Debian 包的 `Maintainer`、man page 的作者行都用这个身份。邮箱用的是 GitHub 提供的
  noreply 地址，提交能关联到账号，我不希望暴露真实邮箱。
- 发布工具链固定在 Alpine 3.24.0 minirootfs（校验 SHA256），缓存在
  `build/musl/rootfs`。rootfs 里的软件包版本来自 Alpine 仓库，所以
  `--rebuild-toolchain` 可能带上更新的 GCC/musl 修订版。
- 静态 musl 有两条代价：`~user` 只能解析 `/etc/passwd` 里的用户（不再经过 NSS
  查 LDAP/SSSD），而 `$HOME` 设置时优先用环境变量；另外 ASan/UBSan/valgrind
  不再属于发布流程，验证依赖 `ctest` 与 `build_static.sh` 里的静态检查。
- musl 或 libstdc++ 的安全修复只能通过重新发布二进制到达用户，因为库已经嵌入
  可执行文件。

之后想补的：`debian/` 正式源码包布局 + debhelper（这样 `dpkg-buildpackage`、
`sbuild`、PPA 那套都能直接用），以及把 core 静态库和头文件拆一个 `-dev` 包。

## 架构约定

这几条是刻意为之的，直接决定将来接 Qt6 时要不要返工：

1. **core 不打印、不退出、不抛异常**。所有函数返回 `Result<T>` 或 `Error`
   （`ErrorCode` + 消息 + 路径），只有 `cli/` 决定怎么展示。
   Qt 那边换成 `QMessageBox` / `statusBar` 即可。

2. **`FileEntry` 是纯数据**。将来写 `QAbstractItemModel` 时把它喂给 view，
   不需要改 core。`list_directory()` 是"路径进、条目数组出"的无状态调用。

3. **耗时操作自带进度与取消**。`copy_path` / `move_path` / `remove_path` 接受
   `OperationOptions`，其中 `on_progress` 是回调、`is_cancelled` 是取消令牌。
   终端前端把回调实现成一行 `\r` 刷新的进度条，Qt 前端把它实现成 emit signal，
   core 的签名不用动。Ctrl-C 也接到同一个取消令牌上：复制到一半按 Ctrl-C
   会得到一个干净的 `cancelled` 错误，而不是把进程打死。检索用的是同一套约定
   （`SearchOptions::on_progress` 与 `is_cancelled`），只是进度字段换成了已扫描
   条目数，另外匹配结果可以通过 `on_match` 流式输出而不是先收集。

4. **UI 状态放在 `Session`**。它只保存 cwd、home、上一个目录，并负责把用户
   输入解析成绝对路径。Qt 前端同样复用它。

5. **路径统一按 UTF-8 走**（`to_utf8` / `from_utf8`），终端和 Qt 都是 UTF-8，
   避免中文文件名在某一层被截断。

## 已实现 / 尚未实现

已实现：列目录（含自然序、目录优先）、详细信息、复制、移动、删除、建目录、
目录树、按名字或内容检索（glob / 子串 / 正则，带类型、深度、大小与时间过滤，
可一次搜多个起点）、进度显示、取消、错误分类、交互式行编辑与命令/路径/短选项
Tab 补全，以及六套测试（shell 冒烟测试、真实 PTY 补全测试、补全引擎检查、
复制原子性的 core 测试、检索取消检查、纯函数的单元检查）。Qt6 前端尚未实现，
当前只有终端界面。

几个需要知道的行为：

- 复制先写临时文件（目标目录下的 `.名字.mxplorer-partial.<pid>.<n>`），写完再
  `rename` 就位，所以被中断或失败的复制不会留下半成品目标文件，临时文件也会
  被清理。
- Ctrl-C 能干净地取消正在进行的复制/检索。复制等耗时操作期间的 SIGTERM、
  SIGKILL 仍没有接管，因此可能留下隐藏的 `.mxplorer-partial` 文件——和普通
  `cp` 被终止后留下半成品目标文件是同一性质。提示符上收到 SIGTERM、SIGHUP 或
  SIGQUIT 时，行编辑器会先恢复终端再退出。
- `-p` 保留权限位（含 setuid、setgid、sticky）、时间戳、扩展属性（ACL 以
  `system.posix_acl_*` 属性存储），以及同一棵树内文件之间的硬链接关系。
  属主不保留：那需要 root 权限。
- 稀疏文件通过 `SEEK_DATA` / `SEEK_HOLE` 复制，空洞仍是空洞，不会被写成实打实
  的零。
- 名字按自然序排列（`file2` 在 `file10` 前），ASCII 部分忽略大小写；其余按
  无符号字节序比较，折叠后相同的名字再按原始字节排序，所以顺序是全序且可复现。
- 读不到属性的条目仍会列出（`ls -l` 显示 `?` 与未知字段）并给出警告，不再悄悄
  消失。整棵目录打不开仍是硬错误：`ls` 报 `Permission denied`，`tree` 打印
  `[unreadable: ...]` 后继续，`cp -r` / `rm -r` 直接失败。递归复制的新目标树先在
  隐藏的临时目录中完整构建，成功后才原子改名；已存在目标目录的合并复制不是事务。
- 把目录复制或移动进它自身会被拒绝，检查还覆盖用符号链接拼出来的目标写法
  （`link -> src` 时执行 `cp -r src link/inner`），递归复制不会失控地灌进源目录。
- `~user` 与 `~user/路径` 会到 passwd 数据库里查；查不到的用户按普通名字处理。
- 忽略大小写会走进程的 locale 折叠：UTF-8 locale 下非 ASCII 字母也能匹配，没有
  locale 时退化为 ASCII 折叠。
- 检索在 `collect_matches` 为假时通过 `SearchOptions::on_match` 流式输出，超大树
  也只占有限内存；想要结果数组的调用方可以保持默认收集。
- 补全在终端前端内用 POSIX 终端接口和 `list_directory()` 实现，不往 core 引入
  readline/Qt 依赖。
- 明确不做：保留属主、reflink/clone 优化、持久化命令历史、双横线长选项、
  复制管道/套接字/设备文件、单个超大目录的进度、回收站与撤销。

## 代码风格

统一使用 **Allman** 花括号风格：任何左花括号都独占一行（命名空间、类定义、函数、
`if` / `else` / `for` / `while` / `switch`、lambda、构造函数初始化列表之后都是如此）。
风格由仓库根目录的 `.clang-format` 固化：4 空格缩进、100 列、注释与 include 不重排、
短函数不压成单行，唯一的例外是 `switch` 里的 `case X: return Y;`，它允许保持一行。

## 许可证

GPL-3.0-or-later，全文见 [LICENSE](LICENSE)（未改动的 GPLv3 文本）；Debian 包里对应
`/usr/share/doc/mxplorer/copyright`。每个源文件头部都有 SPDX 标识。

选 GPLv3 是为了将来接 Qt6 GUI 时能按 GPLv3 选项静态链接 Qt，做成单个自包含的二进制
包。要注意随之而来的义务：发布静态链接的二进制时，必须提供完整的对应源代码（本项目
代码 + 所用 Qt 版本源码 + 构建脚本）。LGPLv3 虽然也允许静态链接，但要求让接收者能
重新链接（例如提供目标文件），GPLv3 没有这层麻烦，代价是整个作品都按 GPLv3 发布，但
无伤大雅。
