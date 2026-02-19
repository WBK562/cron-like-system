# POSIX Cron System with Async Logging

A lightweight, Linux-based Task Scheduler (Cron) implementation using POSIX Message Queues for IPC and POSIX Timers for job execution. The project includes a built-in asynchronous logging library controlled via signals.

## Features
* **Task Scheduling:** Supports one-time tasks (relative or absolute time) and cyclic tasks.
* **IPC Communication:** Client-Server architecture based on POSIX Message Queues (`mqueue`).
* **Multi-threaded Server:** Handles multiple requests and manages a dynamic list of tasks using a thread-safe Doubly Linked List.
* **Asynchronous Logging:** Integrated logging library that supports dynamic log-level switching and state dumping via signals.
* **Job Execution:** Uses `fork` and `execv` to run external programs as scheduled tasks.

## Getting Started

### Prerequisites
* Linux-based OS.
* GCC Compiler.
* POSIX Real-time extensions (available in most modern Linux distros).

### Compilation
Build the project using the provided Makefile:
```bash
make
