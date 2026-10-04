# Mxplorer

一个跑在 Linux 上的简易文件资源管理器。当前界面是终端里交互式 shell，
所有文件系统逻辑集中在一个不依赖任何 UI 的静态库 `mxplorer_core` 里，
将来的 Qt6 前端只是再加一个可执行文件，不碰 core。

名字来自作者姓名缩写 M.R.G. 的首字母与 Explorer 的结合。品牌写作 Mxplorer，而命令
名、包名、命名空间与头文件目录一律小写 `mxplorer`（类似 Git 与 `git`）。

项目还在开发中，没有发布，也不做版本迭代；`build/`、`dist/` 这类产物随时可以删掉
重新生成。

## 构建

需要 CMake 3.20+、支持 C++20 的编译器（GCC 13+ / Clang 16+）。
除标准库外只用到几个 POSIX 接口：core 用 `pwd.h`（取家目录）和 `fnmatch.h`（检索
的通配符匹配），前端用 `unistd.h` / `sys/ioctl.h`（终端判断与宽度）和 `signal.h`。

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
└── tests/smoke_test.sh       # 端到端检查，由 ctest 调用
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
| `cp [-r] [-f] [-p] src... dst` | 复制。`dst` 是已存在的目录时复制到其内部（同 cp(1)）；符号链接被重建而不跟随 |
| `mv [-f] src... dst` | 移动/改名。同一文件系统走 `rename(2)`，跨设备自动退化为复制+删除 |
| `rm [-r] [-f] path...` | 删除。目录需要 `-r`；`-f` 让不存在的路径静默通过 |
| `mkdir [-p] path...` | 建目录，`-p` 建父目录且对已存在目录静默 |
| `stat path...` | 显示类型、大小、mtime、权限位 |
| `tree [path] [-L depth]` | 打印目录树 |
| `find <pattern> [path] [选项]` / `search` | 按名字检索，结果逐行打印绝对路径；`-i` 忽略大小写、`-E` 正则、`-F` 字面、`-l` 长格式、`-a` 含隐藏、`-t f\|d\|l` 类型、`-d N` 深度、`-n N` 上限 |
| `help` / `clear` / `exit` | — |

路径支持 `~`、`~`/相对路径、`-`，以及 `'单引号'`、`"双引号"`、反斜杠转义
（`cd "my dir"` 可以正常工作）。

## 打包成 deb

```sh
./packaging/build_deb.sh
# ==> dist/Mxplorer-0.0.1-unstable-amd64.deb

sudo apt install ./dist/Mxplorer-0.0.1-unstable-amd64.deb
mxplorer --version
man mxplorer
```

这台机器上没有装 debhelper，所以脚本不走 `dpkg-buildpackage`：它自己把安装树
铺到 `build/package/` 里（`cmake --install` + `DESTDIR`），写好 control、
md5sums、copyright、changelog，最后交给 `dpkg-deb --build` 成包。
`Depends` 仍然由 `dpkg-shlibdeps` 算出来，所以依赖串和 debhelper 的结果一致。

包内容：

```
/usr/bin/mxplorer
/usr/share/man/man1/mxplorer.1.gz
/usr/share/doc/mxplorer/{copyright,changelog.gz}
```

两点要注意：

- `Maintainer` 和 man page 的作者目前是占位的 `mrg <mrg@localhost>`，要对外发布
  得换成真实身份。
- 依赖是**在本机算出来**的（`libc6 (>= 2.38)`、`libstdc++6 (>= 16)`，因为这里是
  forky + GCC 16）。也就是说这个包只能装在不低于本机版本的系统上，Debian 12 或
  Ubuntu 22.04 会因为 libstdc++ 太旧而装不上。要支持更老的目标系统，得用
  debootstrap/容器在目标发行版里构建。

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
   条目数。

4. **UI 状态放在 `Session`**。它只保存 cwd、home、上一个目录，并负责把用户
   输入解析成绝对路径。Qt 前端同样复用它。

5. **路径统一按 UTF-8 走**（`to_utf8` / `from_utf8`），终端和 Qt 都是 UTF-8，
   避免中文文件名在某一层被截断。

## 已实现 / 尚未实现

已实现：列目录（含自然序、目录优先）、详细信息、复制、移动、删除、建目录、
目录树、按名字检索（glob / 子串 / 正则，带类型与深度过滤）、进度显示、取消、
错误分类、端到端冒烟测试。

刻意留的坑（想继续做的话）：

- 复制被中断会留下不完整的文件（和 `cp` 行为一致）。要更稳妥就改成写临时文件
  再 `rename` 到目标。
- 不保留硬链接、ACL、xattr，也没做稀疏文件优化。
- 目录里个别条目取不到属性时会静默跳过（不报错、不提示）。整棵目录打不开则是
  硬错误：`ls` 报 `Permission denied`，`tree` 打印 `[unreadable: ...]` 后继续，
  `cp -r` / `rm -r` 直接失败，不会产出不完整的副本。
- 不支持 `~user`。
- `cp` 的默认权限沿用 `umask`，`-p` 才会复制权限位（且不会复制 setuid/setgid）。
- 检索只匹配名字，不搜文件内容；大小写折叠只对 ASCII 可靠；结果在返回前都放在
  内存里（`SearchReport::matches`），所以超大树 + 无上限的长搜索会吃内存。要按
  内容搜或流式输出，得另加一层（`SearchOptions::extra_filter` 就是为内容匹配留
  的接口）。

## 接 Qt6 的路线

Qt6 还没安装（`pkg-config --modversion Qt6Core` 目前找不到）。装好之后大致是：

1. 新建 `gui/`，`find_package(Qt6 COMPONENTS Widgets REQUIRED)`，
   `add_executable(mxplorer_gui ...)` 并链接 `mxplorer_core`，同时在顶层打开
   `-DMXPLORER_BUILD_GUI=ON`（当前这个选项会明确报错提示尚未实现）。
2. 写 `EntryModel : QAbstractItemModel`，内部缓存 `std::vector<FileEntry>`，
   `refresh()` 调用 `list_directory()`；这一层几乎只是搬运字段。
3. 操作放 `QThreadPool` / `QtConcurrent::run`，把 `OperationOptions::on_progress`
   接到 signal 上，UI 线程只更新进度条；取消按钮触发 `is_cancelled`。
4. `QFileSystemWatcher` 监听当前目录的变化来自动刷新。
5. `Session` 继续用，只是路径来源从命令行变成地址栏。
6. 搜索可以直接拿 `search()` 当后台任务跑：每找到一个就让 `SearchReport::matches`
   增长，用 `on_progress` 更新进度条，用同一个取消令牌接停止按钮；搜索框的过滤
   条件对应 `SearchOptions` 里的 `mode` / `filter` / `include_hidden` / `max_depth`。
