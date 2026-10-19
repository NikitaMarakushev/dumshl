# dumshl

A minimal Unix shell written in **modern C++**, built to explore how operating systems
really work: processes, file descriptors, inter-process communication and signals.

No GUI. No third-party libraries. Just C++, POSIX and a terminal.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)
![Build](https://img.shields.io/badge/build-CMake-green)

## Features

### Command execution
- Run any external program available in `$PATH` (`ls`, `cat`, `grep`, `vim`, ...)
- Built-in commands executed in the shell process itself

| Command | Description |
|---------|-------------|
| `cd <path>` | Change the working directory |
| `exit` | Leave the shell |
| `jobs` | List background jobs and their state |
| `kill %<id>` | Send `SIGTERM` to a background job |
| `kill <pid>` | Send `SIGTERM` to a process by PID |

### I/O redirection
```bash
ls -la > files.txt          # redirect stdout (creates / truncates the file)
cat < files.txt             # redirect stdin
sort < input.txt > output.txt   # both at once
```

### Pipelines
Connect an arbitrary number of commands — each command's stdout becomes the
next command's stdin:
```bash
ps aux | grep dumshl
cat data.txt | sort | uniq | wc -l
find . -name "*.cpp" | xargs wc -l
```

### Background execution & job control
```bash
sleep 30 &          # [1] 12345
jobs                # [1] Running    sleep
kill %1             # terminate the job
```
- Background jobs do not block the prompt
- `SIGCHLD` handler reaps finished children, so no zombie processes
- Completion notification is printed when a background job exits

## Requirements

- Linux, macOS or WSL (POSIX system calls)
- A C++17 compiler (`g++` or `clang++`)
- CMake 3.10+
- Git (optional)

No external dependencies beyond the C++ standard library and POSIX.

## Building

```bash
git clone <your-repo-url> dumshl
cd dumshl

cmake -S . -B build
cmake --build build -j

./build/dumshl
```

Alternatively, without CMake:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic src/main.cpp -o dumshl
./dumshl
```

## Usage

```text
dumshl> echo "hello from my shell"
hello from my shell

dumshl> ls -l | grep ".cpp" | wc -l
1

dumshl> echo "saved output" > greeting.txt
dumshl> cat < greeting.txt
saved output

dumshl> sleep 60 &
[1] 4242
dumshl> jobs
[1] Running    sleep
dumshl> kill %1

dumshl> cd /tmp
dumshl> pwd
/tmp

dumshl> exit
Goodbye!
```

### Key implementation details

- **`fork()` + `execvp()`** — every external command runs in a separate process;
  `execvp()` resolves the executable through `$PATH`.
- **`dup2()`** — redirects file descriptors `0` (stdin) and `1` (stdout) to files
  or pipe ends before `execvp()`.
- **`pipe()`** — a pipeline of `N` commands creates `N - 1` pipes. All unused pipe
  ends are closed in both parent and children; otherwise readers would block forever.
- **`waitpid()`** — prevents zombie processes.
- **`sigaction(SIGCHLD, ...)`** — asynchronous reaping of background jobs with
  `WNOHANG`, so the prompt stays responsive.
- **Why `cd` is a builtin** — a child process cannot change its parent's working
  directory, so directory changes must happen inside the shell itself.

## Project structure

```text
dumshl/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── src/
│   └── main.cpp      # parser, builtins, executor, signal handler
└── build/            # generated, ignored by Git
```

## Known limitations

The project is intentionally incremental. Current scope and caveats:

- Whitespace-based tokenizer: no quotes, escape sequences, glob expansion or
  variable expansion yet.
- Only `>` (truncate) and `<` are supported — no `>>`, `2>` or `2>&1`.
- `&` works for single commands; pipelines always run in the foreground.
- Built-ins cannot be used inside a pipeline.
- Basic job control only (`jobs`, `kill`): no `fg`, `bg`, `Ctrl+Z`, and no
  process-group isolation. `Ctrl+C` is not handled and may terminate the shell.
- No command history, tab completion or aliases.
- The child's exit status is not yet propagated to the shell.
- Technical debt: the `SIGCHLD` handler performs console I/O, which is not
  async-signal-safe. Reporting should be deferred to the main loop.

## Roadmap

- [ ] Proper lexer: quotes, escapes and operator tokenization
- [ ] Append (`>>`) and stderr (`2>`, `2>&1`) redirection
- [ ] Exit status handling, `&&` and `||` operators
- [ ] Process groups and real job control: `fg`, `bg`, `Ctrl+C`, `Ctrl+Z`
- [ ] Command history and line editing
- [ ] Environment variables, expansion and aliases
- [ ] Background pipelines
- [ ] Split `main.cpp` into `parser`, `builtins` and `executor` modules
- [ ] Automated tests and an async-signal-safe reaping design

## Learning outcomes

Building this shell covers:

- Process lifecycle: `fork`, `exec`, `wait`, zombie and orphan processes
- File descriptors and duplication: `open`, `dup2`, `close`
- Inter-process communication with pipes
- Signal handling and reentrancy
- POSIX job control and terminal behavior
- C++ resource management and RAII around OS handles
