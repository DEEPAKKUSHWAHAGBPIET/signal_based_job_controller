# 🧠 Signal-Based Job Controller in C

This program demonstrates how to use **Unix signals** to control and manage **child processes** dynamically.  
It spawns worker processes and gracefully **stops**, **restarts**, or **terminates** them based on signals such as `SIGINT`, `SIGTERM`, and `SIGHUP`.

---

## 📜 Overview

The **Job Controller** acts as a **supervisor** for worker processes.  
Each worker performs a repetitive task (simulated with `sleep` and `printf`).  
The parent process listens for **signals** and manages the lifecycle of its workers accordingly.

---

## ⚙️ Features

- ✅ **Spawn Child Processes** on startup.  
- 🛑 **Gracefully Stop** all children with `SIGTERM`.  
- 🔁 **Restart** children with `SIGHUP`.  
- ❌ **Exit Cleanly** on `SIGINT` (Ctrl+C).  
- 🧩 Demonstrates **inter-process communication** via **signals**.

---

## 🧩 Signal Behavior

| Signal   | Description | Action Taken |
|-----------|--------------|--------------|
| `SIGINT`  | Sent by user (Ctrl+C) | Terminates all child processes and exits the controller. |
| `SIGTERM` | Graceful shutdown request | Stops all running child processes. |
| `SIGHUP`  | Hang-up signal | Restarts all worker processes. |

---

## 🧠 Program Flow

1. **Parent (Controller)** starts and spawns a fixed number of worker child processes.  
2. Each **child** simulates work by printing a message and sleeping periodically.  
3. The **parent** installs custom signal handlers using `sigaction()`.  
4. Based on received signals:
   - It **kills or restarts** worker processes appropriately.
5. Upon termination (`SIGINT`), the program ensures a **clean exit**.

---
