# M4 Decision Record

## 1. Threads and ownership

- **GUI thread:** `MainWindow` and all widgets. Owns the `Workspace`, the current `ProcessingCore`, `state_`, and `generation_`.
- **Build worker:** `run_corpus_build()`, started by `MainWindow::start_build_job()` with `std::async`. At most one exists. `MainWindow` owns its `std::future<BuildResult>`, and `~MainWindow()` cancels and waits for it. A corpus moves from a worker local to `BuildResult`, through the future, to `poll_build()`, and into `processing_core_`. A stale one is destroyed in `poll_build()`.
- **IPC thread:** `IpcServer::serve()`, a `std::thread` owned by `IpcServer`, which `MainWindow` owns through a `unique_ptr`. `IpcServer::stop()` joins it.

**Constraint 5: mutable data reachable from more than one thread**

| Data | Written by | Read by | Why it is safe |
|---|---|---|---|
| Cancel flag (`build_cancel_`) | GUI | GUI, worker | `std::atomic<bool>`; the `shared_ptr` keeps it alive for both |
| `IpcServer::stop_requested_` | GUI | GUI, IPC | `std::atomic<bool>` |
| `listen_fd_` | GUI, before the thread starts and after it is joined | GUI, IPC | Never written while the thread runs |
| `std::future` shared state | Worker | GUI | Synchronized by the standard library |

The worker has its own copy of the `Workspace`, made on the GUI thread before it starts. `socket_path_` and `on_add_` never change after construction. The IPC thread uses the `MainWindow` pointer only for a queued `QMetaObject::invokeMethod`, and `~MainWindow()` joins that thread first.

## 2. How a finished build is judged valid

`invalidate_corpus()` and `reset_application()` increment `generation_`. `start_build_job()` saves it in `build_generation_`. When the future is ready, `poll_build()` calls `build_is_current()`, which compares the two. A mismatched result or exception is discarded. The cancel flag only saves time; this comparison is what provides correctness.

## 3. Changes since the checkpoint

- Removed `generation` from `BuildResult`. A failed build returns no result, so `build_generation_` handles both cases.
- Add or Reset during a build now moves to `NotBuilt` immediately, so Build can be clicked again at once. `rebuild_requested_` starts that build after the cancelled worker is collected, which keeps it to one worker.
- Added `CoreFactory` and `set_core_factory()` so tests can force a build failure.

## 4. CR-1 impact

**Changed:** `main()` (the `--ipc-socket` option and starting the server), `~MainWindow()` (stops the server first), and `CMakeLists.txt`. **New:** `MainWindow::start_ipc_server()`, `MainWindow::remote_add_document()`, `IpcServer`, `ipc_protocol.hpp`, and `tools/aiws_send.cpp`.

**Unchanged:** `add_document_file()`, `invalidate_corpus()`, `build_corpus()`, `poll_build()`, and `build_is_current()`. A remote add arrives on the GUI thread and calls `add_document_file()`, the same path as Add Document, so the existing corpus-state logic already covers it.

**Message format:**

- Request: 4-byte big-endian unsigned N (1 to 65535), then N bytes: absolute path, UTF-8, no terminator.
- Reply: 1 status byte (0 accepted, 1 rejected), 4-byte big-endian unsigned M (0 to 4096), then M bytes: UTF-8 reason. M is 0 when accepted.

Length prefixes allow newlines in paths and let `read_exact()` reassemble split data. `aiws_send` resolves relative paths because only it knows its working directory. `IpcServer` checks readability and replies without waiting for the GUI thread.

**Decisions on ambiguous points:** A duplicate path returns true and changes nothing, as in M3. Clients are served one at a time, with a 3 s idle timeout. If the socket cannot be bound, the GUI still runs and prints an error. Usage errors in `aiws_send` exit with 2.

## 5. Fault switches

| Macro | Where | Caught by |
|---|---|---|
| `AIWS_FAULT_ACCEPT_STALE` | `build_is_current()` accepts every build; `cancel_running_build()` does not cancel | `add_during_build_discards_stale_result`, `reset_during_build_discards_stale_result`, `build_clicked_right_after_add_reflects_all_documents`, `remote_add_during_build_invalidates_it` |
| `AIWS_FAULT_LOSE_FAILURE` | `report_build_failure()` returns without reporting, so the state stays `Building` | `failed_build_is_reported_and_app_stays_usable` |

## 6. AI-use disclosure

I used Claude (Claude Code) throughout M4. It drafted my design-note decisions, generated the implementation and tests, drafted this record, and prepared notes I used for my pair session. During the session, I typed the poll timer, poll_build(), and add_document_file(). I then developed the rest of my code and used claude to organize all writien code and files together. I checked my understanding by reviewing the code and running the tests and fault builds.

## Pair programming log

**Partner:** Zachary LaCava

| Session | Date | Start–End | Whose code | Part of the code | Role swaps |
|---|---|---|---|---|---|
| 1 | Nov 4, 2026 | 7:22–7:52 pm | Mine | Background build: poll timer, `poll_build()`, and `add_document_file()` | I drove first; swapped to my partner at 7:37 pm |
| 2 | Nov 4, 2026 | 8:00–8:30 pm | Partner's | CR-1 sockets: starting and stopping threads in `main.cpp` and `sendResponse()` in `ipc.cpp` | My partner drove first; swapped to me at 8:15 pm |

**My contribution as navigator:** I noticed that `add_document_file()` updates the GUI, so it cannot be called directly from the socket thread. I suggested using a queued `invokeMethod` call so it runs on the GUI thread instead.

**My partner's contribution as navigator:** While I was working on `poll_build()`, my partner helped catch some typos and suggested adding `catch (...)` to handle any unexpected errors.

**What kind of interaction worked best:** Going over what each function needed to do before writing the code worked best for us. This helped us catch problems early instead of finding them when compiling.
