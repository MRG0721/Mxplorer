# Mxplorer
**[English](README.md) | [简体中文](README.zh-CN.md)**

一个跑在 Linux 上的简单的文件资源管理器。当前界面是终端里交互式 shell，
所有文件系统逻辑集中在一个不依赖任何 UI 的静态库 `mxplorer_core` 里，
将来的 Qt6 前端只是再加一个可执行文件，不碰 core。

名字来自作者的 Github 用户名 (username)，即 MRG0721 的首字母与 Explorer 的结合。
其写作 Mxplorer，而命令名、包名、命名空间与头文件目录则是一律小写 `mxplorer`
（类似 Git 与 `git`）。

项目还在开发中，没有发布，也不做版本迭代；`build/`、`dist/` 这类产物随时可以删掉
重新生成。

## 构建

需要 CMake 3.20+、支持 C++20 的编译器（GCC 13+ / Clang 16+）。
除标准库外只用到几个 POSIX 接口：core 用 `pwd.h`（取家目录）、`fnmatch.h`（检索的
通配符匹配）、`sys/stat.h`（属性与时间戳）、`sys/xattr.h`（扩展属性与 ACL）以及少量
POSIX I/O 调用（`open` / `read` / `write` / `lseek` / `ftruncate` / `utimensat`）；
前端用 `unistd.h` / `sys/ioctl.h`（终端判断与宽度）和 `signal.h`。

```sh
cmake -S . -B build -G Ninja
cmake --build build

./build/cli/mxplorer                              # 交互式 shell
./build/cli/mxplorer ls -l /tmp                   # 也可以只跑一条命令
ctest --test-dir build --output-on-failure    # 冒烟测试
```

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
│   └── src/{main,shell}.cpp
├── packaging/                # deb 打包：man page、control 模板、打包脚本
│   └── build_deb.sh
└── tests/                    # 三套测试，都由 ctest 驱动
    ├── smoke_test.sh         # 终端前端的端到端检查
    ├── core_operations_test.cpp  # core 的定点检查（复制原子性）
    └── core_utils_test.cpp   # 纯函数检查：折叠、路径、排序、错误
```

## 用法

交互模式（提示符显示当前目录，`cd` 状态会保持）：

```
$ ./build/cli/mxplorer
mxplorer 0.0.1 | type 'help' for commands, 'exit' to quit
mxplorer:~$ cd mxplorer
mxplorer:~/mxplorer$ ls
...
```

非交互模式一次只执行一条命令，进程结束状态就是命令的结果（可直接用在脚本里）：

```sh
./build/cli/mxplorer tree . -L 2
./build/cli/mxplorer cp -r src backup/ && echo done
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

## 打包成 deb

```sh
./packaging/build_deb.sh
# ==> dist/Mxplorer-0.0.1-unstable-amd64.deb

sudo apt install ./dist/Mxplorer-0.0.1-unstable-amd64.deb
mxplorer --version
man mxplorer
```

我的设备上没有装 debhelper，所以脚本不走 `dpkg-buildpackage`：它自己把安装树
铺到 `build/package/` 里（`cmake --install` + `DESTDIR`），写好 control、
md5sums、copyright、changelog，最后交给 `dpkg-deb --build` 成包。
`Depends` 仍然由 `dpkg-shlibdeps` 算出来，所以依赖串和 debhelper 的结果一致。

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
- 依赖是**在本机算出来**的（`libc6 (>= 2.38)`、`libgcc-s1 (>= 3.0)`、`libstdc++6 (>= 16)`，因为这里是
  forky + GCC 16）。也就是说这个包只能装在不低于本机版本的系统上，Debian 13 或
  Ubuntu 26.04 这些东西可能会因为 libstdc++ 太旧而装不上。要支持更老的目标系统，得用
  debootstrap/容器在目标发行版里构建。
- 暂时**不考虑**兼容性：我暂时是不考虑对于所有系统的兼容性的，正如上一条所言，"依赖
  是在本机算出来的"。我现阶段毕竟还是开发初期，该项目目前本就是稚嫩的。

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
可一次搜多个起点）、进度显示、取消、错误分类，以及三套测试（shell 冒烟测试、
复制原子性的 core 测试、纯函数的单元检查）。Qt6 前端尚未实现，当前只有终端界面。

几个需要知道的行为：

- 复制先写临时文件（目标目录下的 `.名字.mxplorer-partial.<pid>.<n>`），写完再
  `rename` 就位，所以被中断或失败的复制不会留下半成品目标文件，临时文件也会
  被清理。
- Ctrl-C 能干净地取消正在进行的复制/检索。其它终止信号（SIGTERM、SIGKILL）
  没有接管，因此可能留下隐藏的 `.mxplorer-partial` 文件——和普通 `cp` 被终止
  后留下半成品目标文件是同一性质。
- `-p` 保留权限位（含 setuid、setgid、sticky）、时间戳、扩展属性（ACL 以
  `system.posix_acl_*` 属性存储），以及同一棵树内文件之间的硬链接关系。
  属主不保留：那需要 root 权限。
- 稀疏文件通过 `SEEK_DATA` / `SEEK_HOLE` 复制，空洞仍是空洞，不会被写成实打实
  的零。
- 名字按自然序排列（`file2` 在 `file10` 前），ASCII 部分忽略大小写；其余按
  无符号字节序比较，折叠后相同的名字再按原始字节排序，所以顺序是全序且可复现。
- 读不到属性的条目仍会列出（`ls -l` 显示 `?` 与未知字段）并给出警告，不再悄悄
  消失。整棵目录打不开仍是硬错误：`ls` 报 `Permission denied`，`tree` 打印
  `[unreadable: ...]` 后继续，`cp -r` / `rm -r` 直接失败，不会产出不完整的副本。
- 把目录复制或移动进它自身会被拒绝，检查还覆盖用符号链接拼出来的目标写法
  （`link -> src` 时执行 `cp -r src link/inner`），递归复制不会失控地灌进源目录。
- `~user` 与 `~user/路径` 会到 passwd 数据库里查；查不到的用户按普通名字处理。
- 忽略大小写会走进程的 locale 折叠：UTF-8 locale 下非 ASCII 字母也能匹配，没有
  locale 时退化为 ASCII 折叠。
- 检索在 `collect_matches` 为假时通过 `SearchOptions::on_match` 流式输出，超大树
  也只占有限内存；想要结果数组的调用方可以保持默认收集。
- 明确不做：保留属主、reflink/clone 优化、交互式行编辑/历史/补全、双横线长选项、
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
