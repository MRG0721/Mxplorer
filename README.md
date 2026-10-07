# Mxplorer
**[English](README.md) | [简体中文](README.zh-CN.md)**

A simple file explorer that runs on Linux. The current interface is an
interactive shell in the terminal. All the filesystem logic lives in a static
library `mxplorer_core` that doesn't depend on any UI. A future Qt6 frontend
will just be one more executable — it won't touch core.

The name comes from the author's GitHub username: the initials of MRG0721
combined with Explorer. It's written as Mxplorer, while the command name,
package name, namespace, and header directory are all lowercase `mxplorer`
(much like Git versus `git`).

The project is still under development, hasn't been released, and doesn't do
versioned iterations; artifacts like build/ and dist/ can be deleted and
regenerated at any time.

## Building

Requires CMake 3.20+ and a compiler with C++20 support (GCC 13+ / Clang 16+).
Besides the standard library, only a few POSIX interfaces are used:
core uses `pwd.h` (the home directory), `fnmatch.h` (wildcard matching in
search), `sys/stat.h` (attributes and timestamps), `sys/xattr.h` (extended
attributes and ACLs) and a handful of POSIX I/O calls
(`open` / `read` / `write` / `lseek` / `ftruncate` / `utimensat`); the frontend
uses `unistd.h` / `sys/ioctl.h` (terminal detection and width) and `signal.h`.

```sh
cmake -S . -B build -G Ninja
cmake --build build

./build/cli/mxplorer                              # interactive shell
./build/cli/mxplorer ls -l /tmp                   # or just run a single command
ctest --test-dir build --output-on-failure    # smoke test
```

## Project layout

```
mxplorer/
├── CMakeLists.txt
├── .clang-format             # Allman brace style, 4 spaces / 100 columns
├── LICENSE                   # GPL-3.0-or-later, unmodified full GPLv3 text
├── core/                     # mxplorer_core: no Qt, no printing, no exit(), no exceptions
│   ├── include/mxplorer/
│   │   ├── callback.hpp      # CancelToken: a UI-agnostic "request stop" callback
│   │   ├── error.hpp         # ErrorCode / Error / Result<T>
│   │   ├── entry.hpp         # FileEntry: all the data for a single entry
│   │   ├── directory.hpp     # list_directory / stat_path, sorting and natural-order comparison
│   │   ├── operations.hpp    # copy / move / remove / mkdir + progress callback + cancel token
│   │   ├── search.hpp        # search by name: glob/substring/regex + filters + progress + cancel
│   │   ├── path_utils.hpp    # UTF-8 conversion, ~ expansion, ~/ display
│   │   └── session.hpp       # current-directory state, parses user input into absolute paths
│   └── src/
├── cli/                      # terminal frontend: parses the command line, prints results, renders progress
│   └── src/{main,shell}.cpp
├── packaging/                # deb packaging: man page, control template, packaging script
│   └── build_deb.sh
└── tests/                    # all three suites are driven by ctest
    ├── smoke_test.sh         # end-to-end check of the terminal front-end
    ├── core_operations_test.cpp  # focused core checks (copy atomicity)
    └── core_utils_test.cpp   # pure helpers: folding, paths, ordering, errors
```

## Usage

Interactive mode
(the prompt shows the current directory, and `cd` state is preserved):

```
$ ./build/cli/mxplorer
mxplorer 0.0.1 | type 'help' for commands, 'exit' to quit
mxplorer:~$ cd mxplorer
mxplorer:~/mxplorer$ ls
...
```

Non-interactive mode runs exactly one command and the process exit status is
the command's result (so it can be used directly in scripts):

```sh
./build/cli/mxplorer tree . -L 2
./build/cli/mxplorer cp -r src backup/ && echo done
```

| Command | Description |
| --- | --- |
| `ls [-a] [-l] [path]` / `ll` | List a directory. `-a` includes hidden entries, `-l` is long format; directories are marked with `/` and symlinks with `@` |
| `cd [path]` | Change directory; `cd` goes home, `cd -` goes back to the previous directory |
| `pwd` | Print the current directory |
| `cp [-r] [-f] [-p] src... dst` | Copy. When `dst` is an existing directory, copies into it (same as `cp`(1)); symlinks are recreated rather than followed. `-p` keeps permissions, timestamps, hard links and extended attributes (ACLs), and sparse files stay sparse |
| `mv [-f] [-p] src... dst` | Move/rename. Uses `rename`(2) on the same filesystem, and automatically degrades to copy+delete across devices |
| `rm [-r] [-f] path...` | Remove. Directories require `-r`; `-f` lets nonexistent paths pass silently |
| `mkdir [-p] path...` | Create directories; `-p` creates parent directories and is silent about already-existing ones |
| `stat path...` | Show type, size, mtime, permission bits |
| `tree [path] [-L depth]` | Print a directory tree |
| `find <pattern> [path...] [options]` / `search` | Search by name or content, printing absolute paths one per line; `-i` ignore case, `-E` regex, `-F` literal, `-l` long format, `-a` include hidden, `-t f\|d\|l` type, `-d N` depth, `-n N` limit, `-c text` content, `-s ±N[KMG]` size, `-m ±N[dhms]` age |
| `help` / `clear` / `exit` / `quit` | — |

Paths support `~`, `~user`, `~`/relative/path, `-`, as well as `'single quotes'`,
`"double quotes"`, and backslash escapes (`cd "my dir"` works fine).

## Packaging as a deb

```sh
./packaging/build_deb.sh
# ==> dist/Mxplorer-0.0.1-unstable-amd64.deb

sudo apt install ./dist/Mxplorer-0.0.1-unstable-amd64.deb
mxplorer --version
man mxplorer
```

My machine doesn't have debhelper installed, so the script doesn't go through
`dpkg-buildpackage`: it lays out the install tree itself under `build/package/`
(`cmake --install` + `DESTDIR`), writes control, md5sums, copyright, and
changelog, then hands it to `dpkg-deb --build` to produce the package.
`Depends` is still computed by `dpkg-shlibdeps`, so the dependency string
matches what debhelper would produce.

Package contents:

```
/usr/bin/mxplorer
/usr/share/man/man1/mxplorer.1.gz
/usr/share/doc/mxplorer/{copyright,changelog.gz}
```

Notes:

- The maintainer identity is
  `MRG0721 <271227737+MRG0721@users.noreply.github.com>`: commit author, Debian
  package Maintainer, and the man page author line all use this identity.
  The email is the noreply address GitHub provides, so commits can be linked to
  the account, and I don't want to expose my real email.
- Dependencies are **computed on this machine**
(`libc6 (>= 2.38)`, `libgcc-s1 (>= 3.0)`, `libstdc++6 (>= 16)`, since this is forky + GCC 16).
In other words, this package can only be installed on systems no older than
this one; things like Debian 13 or Ubuntu 26.04 may fail to install
because their libstdc++ is too old. To support older target systems, you'd
need to build inside the target distro using debootstrap/a container.
- For now I'm **not worrying about compatibility**:
as I said in the previous point, "dependencies are computed on this machine."
At this stage I'm still in early development, and the project itself is
admittedly immature.

Things I want to add later:
a proper `debian/` source-package layout + debhelper
(so `dpkg-buildpackage`, `sbuild`, PPA, and the rest can just be used directly)
, and splitting the core static library and headers into a `-dev` package.

## Architecture conventions

These points are deliberate, and they directly determine whether
hooking up Qt6 later will require rework:

1. **core doesn't print, exit, or throw**.
All functions return `Result<T>` or `Error` (`ErrorCode` + message + path);
only `cli/` decides how to present them. On the Qt side,
this just becomes `QMessageBox` / `statusBar`.

2. **`FileEntry` is pure data**. When writing a `QAbstractItemModel` later,
feed it to the view — no need to change core. `list_directory()` is
a stateless call: path in, array of entries out.

3. **Long-running operations carry their own progress and cancellation**.
  `copy_path` / `move_path` / `remove_path` accept `OperationOptions`,
  where `on_progress` is a callback and `is_cancelled` is a cancel token. The
  terminal frontend implements the callback as a one-line `\r`-refreshed
  progress bar; the Qt frontend implements it as emitting a signal — core's
  signature doesn't need to change.
  Ctrl-C is hooked up to the same cancel token too:
  pressing Ctrl-C halfway through a copy yields a clean `cancelled` error
  instead of killing the process. Search uses the same convention
  (`SearchOptions::on_progress` and `is_cancelled`),
  except the progress field becomes the number of entries scanned, and matches
  can be streamed through `on_match` instead of collected.

4. **UI state lives in `Session`**. It only stores cwd, home,
and the previous directory, and is responsible for parsing user input into
absolute paths. The Qt frontend reuses it the same way.

5. **Paths are uniformly handled as UTF-8 (`to_utf8` / `from_utf8`)**;
both the terminal and Qt are UTF-8, avoiding non-English filenames being
truncated at some layer.

## Implemented / not yet implemented

Implemented: directory listing (with natural ordering, directories first),
detailed info, copy, move, remove, mkdir, directory tree, search by name or
content (glob / substring / regex, with type, depth, size and age filters,
several roots at once), progress display, cancellation, error classification,
and three test suites (a shell smoke test, core copy-atomicity checks and
unit checks for the pure helpers).
The Qt6 front end does not exist yet; the terminal is the only interface.

Behavior worth knowing about:

- A copy writes a temporary file (`.name.mxplorer-partial.<pid>.<n>` next to
  the destination) and renames it into place, so an interrupted or failed copy
  never publishes a partial file, and the temporary file is cleaned up.
- Ctrl-C cancels a running copy/search cleanly. Other termination signals
  (SIGTERM, SIGKILL) are not handled, so a hidden `.mxplorer-partial` file can
  survive them, just like an ordinary `cp` leaves a partial destination.
- With `-p`, permission bits (setuid, setgid and the sticky bit included),
  timestamps, extended attributes (ACLs are stored as `system.posix_acl_*`
  attributes) and hard links between files copied in one tree are preserved.
  Ownership is not preserved: that would need root.
- Sparse files are copied through `SEEK_DATA` / `SEEK_HOLE`, so holes stay
  holes instead of turning into written zeroes.
- Names are ordered naturally (`file2` before `file10`) with ASCII case
  folding; the rest of the comparison is unsigned byte order, and names that
  fold to the same key are ordered by their raw bytes, so the sort is total
  and reproducible.
- An entry whose attributes cannot be read is still listed, with unknown
  metadata (`?` in `ls -l`) and a warning, instead of disappearing silently.
  An entire directory that cannot be opened stays a hard error: `ls` reports
  `Permission denied`, `tree` prints `[unreadable: ...]` and continues, and
  `cp -r` / `rm -r` fail rather than producing an incomplete copy.
- Copying or moving a directory into itself is refused, and the check also
  covers symlinked spellings of the destination (`cp -r src link/inner` with
  `link -> src`), so a recursive copy cannot run away inside the source tree.
- `~user` and `~user/path` are looked up in the passwd database; a user that
  does not exist is treated as a plain name.
- Case-insensitive matching folds through the process locale, so non-ASCII
  letters work under a UTF-8 locale and degrade to ASCII folding without one.
- Search results are streamed through `SearchOptions::on_match` when
  `collect_matches` is false, so a huge search uses bounded memory; a caller
  that wants the vector can keep the default.
- Still deliberately out of scope: ownership preservation, reflink/clone
  optimization, interactive line editing/history/completion, long options,
  copying FIFOs/sockets/device nodes, progress for a single huge directory,
  and recycle bin/undo.

## Code style

**Allman** brace style throughout: every opening brace gets its own line
(namespaces, class definitions, functions,
`if` / `else` / `for` / `while` / `switch`, lambdas,
and after constructor initializer lists — all of them).
The style is pinned by the `.clang-format` at the repo root: 4-space indent,
100 columns, comments and includes not reordered, and short functions not
collapsed onto a single line. The only exception is that `case X: return Y;`
inside a `switch` is allowed to stay on one line.

## License

GPL-3.0-or-later; the full text is in [LICENSE](LICENSE)
(unmodified GPLv3 text); inside the Debian package it corresponds to
`/usr/share/doc/mxplorer/copyright`.
Every source file has an SPDX identifier at the top.

GPLv3 was chosen so that when a Qt6 GUI is added later, Qt can be
statically linked under the GPLv3 option, producing a single self-contained
binary package. Be aware of the obligations that come with it: when
distributing a statically linked binary, you must provide the complete
corresponding source code
(this project's code + the source of the Qt version used + the build scripts).
LGPLv3 also permits static linking, but requires letting recipients relink
(for example, by providing object files); GPLv3 doesn't have that hassle — the
cost is that the whole work is released under GPLv3, but that's no big deal.
