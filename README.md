# SemCmd - Command Line Synchronization Tool

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C Standard](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://www.linux.org/)

## Description

**`semcmd`** is a powerful yet simple command-line tool for **process synchronization** that brings the robustness of POSIX semaphores to shell scripting. Whether you're coordinating parallel tasks, protecting critical sections, or implementing synchronization barriers, `semcmd` provides an intuitive interface for managing locks directly from the command line.

Built on **named POSIX semaphores**, `semcmd` enables developers to create, acquire, release, and manage locks with simple commands like `create`, `lock`, `unlock`, and `destroy`. Its **automatic namespace isolation** ensures that different users and projects never interfere with each other, making it safe for multi-user environments.

Originally developed for the _"ISS - Initiation au Système et au Shell"_ course at _Sorbonne Université_, `semcmd` serves as both an **educational tool** for teaching concurrent programming concepts and a **production-ready utility** for system administrators and developers who need reliable process synchronization in their scripts and applications.

Perfect for scenarios ranging from **database migration scripts** and **log file rotation** to **parallel data processing** and **resource coordination**, `semcmd` transforms complex synchronization patterns into simple, readable shell commands.

### Key Features
- **Process synchronization** using named POSIX semaphores
- **User isolation** with automatic namespace prefixing
- **Educational focus** for learning concurrent programming
- **Simple command-line interface** for shell script integration
- **Lightweight** with minimal dependencies

## Installation

### Prerequisites
- GCC compiler with C11 support
- POSIX-compliant system (Linux). Note: named POSIX semaphore behavior may vary on macOS.
- Make utility

### Build from Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/ClaquetteSocket/semcmd.git
   cd semcmd
   ```

2. **Build the project:**
   ```bash
   make
   ```

3. **Installation options:**

   **Local installation (recommended):**
   ```bash
   # Install to user's local directory
   make install PREFIX=~/.local
   # Ensure ~/.local/bin is in your PATH
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
   source ~/.bashrc
   ```

   **System-wide installation:**
   ```bash
   sudo make install
   # Or install to custom prefix
   make install PREFIX=/opt/local
   ```

   **Manual installation:**
   ```bash
   # Local installation
   cp semcmd ~/.local/bin/
   # Or system-wide
   sudo cp semcmd /usr/local/bin/
   # Or add to your PATH
   export PATH=$PATH:$(pwd)
   ```

4. **Verify installation:**
   ```bash
   semcmd --version
   ```

5. **Uninstallation:**
   ```bash
   # Remove installed files
   make uninstall PREFIX=~/.local  # For local installation
   sudo make uninstall             # For system-wide installation
   ```

### Shell Completion

`semcmd` provides intelligent tab completion for both **bash** and **zsh** shells.

**Local installation (recommended):**
```bash
make install-completions PREFIX=~/.local
```

**System-wide installation:**
```bash
sudo make install-completions
```

**Manual installation:**

For **bash**:
```bash
# Local installation
mkdir -p ~/.local/share/bash-completion/completions
cp completions/_semcmd_completion.bash ~/.local/share/bash-completion/completions/semcmd
# Or system-wide
sudo cp completions/_semcmd_completion.bash /usr/share/bash-completion/completions/semcmd
```

For **zsh**:
```bash
# Local installation
mkdir -p ~/.local/share/zsh/site-functions
cp completions/_semcmd ~/.local/share/zsh/site-functions/_semcmd
# Or system-wide
sudo cp completions/_semcmd /usr/share/zsh/site-functions/_semcmd
```

**Features:**

- [x] Command completion (`create`, `lock`, `unlock`, etc.)
- [x] Option completion (`--help`, `--force`, `--locked`, etc.)  
- [x] Smart lock name completion for relevant commands
- [x] Context-aware suggestions based on existing locks

Restart your shell or run `source ~/.bashrc` / `source ~/.zshrc` to activate completion.


## Usage

### Syntax
```bash
semcmd [OPTIONS] <clean|list> | <create|lock|unlock|destroy|infos> <name_lock>
```

### Commands

| Command | Description | Example |
|---------|-------------|---------|
| `create` | Creates a lock (default: UNLOCKED) | `semcmd create mylock` |
| `lock` | Acquires the specified lock | `semcmd lock mylock` |
| `unlock` | Releases the specified lock | `semcmd unlock mylock` |
| `destroy` | Deletes the specified lock | `semcmd destroy mylock` |
| `infos` | Shows status of a specific lock | `semcmd infos mylock` |
| `list` | Shows all your locks | `semcmd list` |
| `clean` | Destroys all your locks | `semcmd clean` |

### Options

| Short | Long | Description |
|-------|------|-------------|
| `-h` | `--help` | Display help and exit |
| `-V` | `--version` | Display version number and exit |
| `-f` | `--force` | Force destruction of locks in use (see warning below) |
| `-l` | `--locked` | Create lock in LOCKED state (barrier setup) |

> [!WARNING]
> Using `--force` with locked semaphores may cause undefined behavior for processes holding the lock.
  
## Examples

### Quick Start
```bash
# Create and check a simple lock
semcmd create test_lock
semcmd infos test_lock    # Output: test_lock lock is UNLOCK
semcmd list              # Shows all your locks

# Use the lock
semcmd lock test_lock
semcmd infos test_lock    # Output: test_lock lock is LOCK
semcmd unlock test_lock

# Clean up
semcmd destroy test_lock
```

### Mutual Exclusion Pattern

Protect a critical section using a mutex semaphore:

```bash
#!/bin/bash
# Create mutex semaphore
semcmd create file_mutex

# Start background processes
for i in {1..5}; do
  {
    semcmd lock file_mutex
    echo "Process $i: Writing to shared file..."
    echo "Process $i data" >> shared_file.txt
    sleep 1  # Simulate work
    semcmd unlock file_mutex
  } &
done

wait
semcmd destroy file_mutex
```

### Barrier Synchronization Pattern

Wait for all processes to reach a synchronization point:

```bash
#!/bin/bash
# Create barrier (initially locked)
semcmd create sync_barrier --locked

# Start worker processes
for i in {1..3}; do
  {
    echo "Worker $i: Doing initial work..."
    sleep $((RANDOM % 3 + 1))
    
    echo "Worker $i: Waiting at barrier..."
    semcmd lock sync_barrier
    echo "Worker $i: Proceeding after barrier"
  } &
done

# Main process releases barrier after delay
sleep 2
echo "Main: Releasing barrier"
semcmd unlock sync_barrier
semcmd unlock sync_barrier
semcmd unlock sync_barrier

wait
semcmd destroy sync_barrier
```

### Namespace Usage

Run multiple isolated script instances:

```bash
# Terminal 1: Production environment
SEMCMD_NAMESPACE=prod ./my_script.sh &

# Terminal 2: Development environment  
SEMCMD_NAMESPACE=dev ./my_script.sh &

# Check isolation
ls /dev/shm/sem.semcmd.*
# Output:
# sem.semcmd.prod.username.my_lock
# sem.semcmd.dev.username.my_lock
```
## Troubleshooting

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| `Permission denied` | No access to `/dev/shm` | Check file system permissions |
| `Lock already exists` | Semaphore wasn't cleaned up | Use `semcmd destroy <name>` or `semcmd clean` |
| `No such lock` | Lock name typo or doesn't exist | Verify with `semcmd list` |
| `Memory allocation failed` | System out of memory | Free system resources |

### Environment Variables

- `SEMCMD_NAMESPACE`: Sets custom namespace for lock isolation
- Example: `export SEMCMD_NAMESPACE=myproject`

## Limitations & Roadmap

### Current Limitations

- **Semaphore Values**: Limited to binary semaphores (0 or 1) for educational simplicity
- **Lock Validation**: Prevents unlocking already unlocked semaphores
- **Platform Support**: POSIX semaphores only (Linux, macOS, etc.)
- **Wait Queues**: Cannot display waiting process count due to POSIX limitations

### Future Enhancements

- [ ] **Counting Semaphores**: Support for Dijkstra-style semaphores (values > 1)
- [ ] **System V Fallback**: Alternative implementation for broader compatibility  
- [ ] **Timeout Support**: `sem_timedwait()` integration for deadlock prevention
- [ ] **Process Monitoring**: Better visibility into semaphore usage
- [ ] **Action Logging**: Add support for logging semaphore actions (create, lock, unlock, destroy) to a file for audit and debugging purposes
- [ ] **Wildcards Support**: Allow `destroy` and `infos` commands to accept wildcard patterns for batch operations (e.g., `semcmd destroy mylock*`)

## Error Codes

When `semcmd` encounters an error, it returns a specific exit code to help diagnose the problem:

| Code | Name | Description | Common Causes |
|------|------|-------------|---------------|
| **0** | `SUCCESS` | Operation completed successfully | - |
| **1** | `USAGE_ERR` | Invalid command usage | Wrong syntax, missing arguments |
| **2** | `CREATE_ERR` | Failed to create semaphore | Lock already exists, permissions |
| **3** | `OPEN_ERR` | Failed to open semaphore | Lock doesn't exist, permissions |
| **4** | `CLOSE_ERR` | Failed to close semaphore | System error, corrupted state |
| **5** | `READ_ERR` | Failed to read semaphore value | System error, invalid semaphore |
| **6** | `UNLINK_ERR` | Failed to delete semaphore | Lock in use (without `--force`), permissions, system error |
| **7** | `LOCK_ERR` | Failed to acquire lock | Interrupted system call |
| **8** | `UNLOCK_ERR` | Failed to release lock | Not locked, system error |
| **9** | `BADVAL_ERR` | Invalid semaphore value | Corrupted semaphore state |
| **10** | `SEARCH_ERR` | Failed to search semaphores | `/dev/shm` access issues |
| **11** | `UNDEF_ERR` | Undefined error | Reserved for future use |
| **12** | `ALLOC_ERR` | Memory allocation failed | Out of memory |
| **13** | `LOGIN_ERR` | Failed to get user login | User database error |

### Using Exit Codes in Scripts
```bash
if ! semcmd create mylock; then
  echo "Failed to create lock (exit code: $?)"
  exit 1
fi
```

## Educational Context

This tool was specifically designed for the **"ISS - Initiation au Système et au Shell"** course at **Sorbonne Université's Licence Informatique**. It provides students with hands-on experience in:

- **Process synchronization** concepts
- **Concurrent programming** patterns  
- **Shell scripting** with parallel processes
- **System programming** with POSIX APIs

## Acknowledgements

Thanks to [Sebastien Monnet](https://www.univ-smb.fr/listic/pages-en/sebastien-monnet-en/), the former head of the ISS course, who created a series of synchronization exercises based on lockfile (a synchronization tool integrated into procmail and based on the existence of a file). Thanks also to [Jonathan Lejeune](https://www.lip6.fr/Jonathan.Lejeune/), who initiated this porting work to named POSIX semaphores.


## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).

```
Copyright (C) 2025 Julien Sopena (@ClaquetteSocket) 

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

For the complete license text, visit: https://www.gnu.org/licenses/gpl-3.0.en.html

## Author

**Julien Sopena (@ClaquetteSocket)**  
[github@claquettesocket.fr](mailto:github@claquettesocket.fr)  

---