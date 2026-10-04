# fman

一个跑在 Linux 上的简易文件资源管理器。当前界面是终端里交互式 shell，
所有文件系统逻辑集中在一个不依赖任何 UI 的静态库 `fman_core` 里，
将来的 Qt6 前端只是再加一个可执行文件，不碰 core。

## 构建

需要 CMake 3.20+、支持 C++20 的编译器（GCC 13+ / Clang 16+）。
除标准库外只用到 POSIX 的 `unistd.h` / `pwd.h` / `sys/ioctl.h`。

```sh
cmake -S . -B build -G Ninja
cmake --build build

./build/cli/fman                              # 交互式 shell
./build/cli/fman ls -l /tmp                   # 也可以只跑一条命令
ctest --test-dir build --output-on-failure    # 冒烟测试
```

## 目录结构

```
fman/
├── CMakeLists.txt
├── core/                     # fman_core：纯标准库，没有 Qt、没有打印、没有 exit()
│   ├── include/fman/
│   │   ├── error.hpp         # ErrorCode / Error / Result<T>
│   │   ├── entry.hpp         # FileEntry：一个条目的全部数据
│   │   ├── directory.hpp     # list_directory / stat_path，排序与自然序比较
│   │   ├── operations.hpp    # copy / move / remove / mkdir + 进度回调 + 取消令牌
│   │   ├── path_utils.hpp    # UTF-8 转换、~ 展开、~/ 显示
│   │   └── session.hpp       # 当前目录状态、把用户输入解析成绝对路径
│   └── src/
├── cli/                      # 终端前端：解析命令行、打印结果、渲染进度
│   └── src/{main,shell}.cpp
└── tests/smoke_test.sh       # 端到端检查，由 ctest 调用
```

## 用法

交互模式（提示符显示当前目录，`cd` 状态会保持）：

```
$ ./build/cli/fman
fman 0.1.0 | type 'help' for commands, 'exit' to quit
fman:~$ cd Documents/Project
fman:~/Documents/Project$ ls
...
```

非交互模式一次只执行一条命令，进程结束状态就是命令的结果（可直接用在脚本里）：

```sh
./build/cli/fman tree . -L 2
./build/cli/fman cp -r src backup/ && echo done
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
| `help` / `clear` / `exit` | — |

路径支持 `~`、`~`/相对路径、`-`，以及 `'单引号'`、`"双引号"`、反斜杠转义
（`cd "my dir"` 可以正常工作）。

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
   会得到一个干净的 `cancelled` 错误，而不是把进程打死。

4. **UI 状态放在 `Session`**。它只保存 cwd、home、上一个目录，并负责把用户
   输入解析成绝对路径。Qt 前端同样复用它。

5. **路径统一按 UTF-8 走**（`to_utf8` / `from_utf8`），终端和 Qt 都是 UTF-8，
   避免中文文件名在某一层被截断。

## 已实现 / 尚未实现

已实现：列目录（含自然序、目录优先）、详细信息、复制、移动、删除、建目录、
目录树、进度显示、取消、错误分类、端到端冒烟测试。

刻意留的坑（想继续做的话）：

- 复制被中断会留下不完整的文件（和 `cp` 行为一致）。要更稳妥就改成写临时文件
  再 `rename` 到目标。
- 不保留硬链接、ACL、xattr，也没做稀疏文件优化。
- 权限不足的目录会被静默跳过（`skip_permission_denied`），只在 `tree` 里显示
  `[unreadable: ...]`。
- 不支持 `~user`。
- `cp` 的默认权限沿用 `umask`，`-p` 才会复制权限位（且不会复制 setuid/setgid）。

## 接 Qt6 的路线

Qt6 还没安装（`pkg-config --modversion Qt6Core` 目前找不到）。装好之后大致是：

1. 新建 `gui/`，`find_package(Qt6 COMPONENTS Widgets REQUIRED)`，
   `add_executable(fman_gui ...)` 并链接 `fman_core`，同时在顶层打开
   `-DFMAN_BUILD_GUI=ON`（当前这个选项会明确报错提示尚未实现）。
2. 写 `EntryModel : QAbstractItemModel`，内部缓存 `std::vector<FileEntry>`，
   `refresh()` 调用 `list_directory()`；这一层几乎只是搬运字段。
3. 操作放 `QThreadPool` / `QtConcurrent::run`，把 `OperationOptions::on_progress`
   接到 signal 上，UI 线程只更新进度条；取消按钮触发 `is_cancelled`。
4. `QFileSystemWatcher` 监听当前目录的变化来自动刷新。
5. `Session` 继续用，只是路径来源从命令行变成地址栏。
