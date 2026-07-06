# minitop

`minitop` is a lightweight, low-overhead process monitor and multi-threaded task scheduler written in C++17. It operates in two main modes: a real-time process monitor (similar to the Linux `top` utility) and an asynchronous, priority-based task scheduler queue.

This project is built from scratch with an emphasis on systems-level programming concepts, resource constraints, multi-threading synchronization, and cross-platform portability between Windows and Linux.

---

## Key Features

### 1. Dual-Mode Operation
* **Process Monitor Mode (Default):** Periodically polls process lists and displays system memory, Process IDs (PIDs), process names, CPU utilization (%), and resident memory usage (RSS in MB).
* **Task Scheduler Mode:** Operates an asynchronous background thread pool that accepts function objects with execution delays and execution priorities.

### 2. Multi-threaded Architecture
* **Decoupled Polling & UI:** A background collector thread queries system files and writes snapshots to shared memory, while the main thread renders the UI and reads user input. This eliminates redraw stutter.
* **Thread Compatibility Layer:** Built with a custom abstraction (`thread_compat.h`) mapping C++11 multithreading semantics to Windows native Slim Reader/Writer (SRW) Locks and Windows Condition Variables, enabling native C++ thread-safety under standard Windows MinGW compiler toolchains.
* **Instant Terminal Input:** Leverages POSIX terminal settings (echo and canonical mode overrides) on Linux and `conio.h` on Windows to process interactive hotkeys (like sorting switches) instantly without blocking execution.

### 3. Graceful Signal Handling
* Catches termination signals (`SIGINT` / Ctrl+C and `SIGTERM` / Terminate) to perform clean teardowns, restore the terminal cursor, reset terminal raw settings, and delete temporary simulated resource files.

### 4. Custom Configuration & Flags
* Reads default variables from a `minitop.conf` config file and supports overriding them using standard POSIX `getopt_long` command-line flags.

---

## System Design & Portability

On Linux systems, `minitop` directly parses virtual filesystem files like `/proc/meminfo`, `/proc/stat`, and `/proc/[pid]/stat`.

To run natively on Windows systems for local development without complex virtualization setups, `minitop` implements a **Dual-Mode Mocking Engine** (`proc_mock.cpp`). It queries Windows performance counters and writes standard-formatted mock `/proc` files into a local folder. The parser reads from this folder using standard POSIX directory iterations (`dirent.h`), making the parsing and UI rendering code identical on both operating systems.

---

## Getting Started

### Prerequisites
* A C++17 compiler (GCC, Clang, or MSVC)
* CMake (Version 3.15 or higher)

### Build Instructions

To configure and compile the project using CMake:

```bash
# 1. Generate makefiles (on Windows, specify the MinGW generator if using MinGW)
cmake -B build -S . -G "MinGW Makefiles"

# 2. Compile the binaries
cmake --build build
```

This compiles two targets in the `build/` directory:
1. `minitop` (the main application)
2. `test_parser` (the unit test suite)

### Running the Application

To run the live process monitor:
```bash
# Run with defaults
./build/minitop

# Run with a 2.5-second refresh delay, sorted by memory
./build/minitop -d 2.5 --sort=mem

# Monitor only a single PID
./build/minitop -p 1948
```

To run in task scheduler mode:
```bash
./build/minitop --mode=scheduler
```

To execute unit tests:
```bash
./build/test_parser
```

---

## Configuration File (`minitop.conf`)

You can adjust default values inside `minitop.conf` in the project root:
```ini
# minitop config file
delay = 1.5
sort = cpu
mode = monitor
```

