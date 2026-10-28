# M4 Design Note

## 1. Components and Threads

| Component | Responsibility | Thread |
|---|---|---|
| `MainWindow::build_corpus()` | Copies the Workspace, saves the generation number, creates a cancel flag, and starts the build using `std::async`. It also sets the state to Building and starts the timer. | GUI |
| `run_corpus_build(...)` | Creates a new `ProcessingCore` using the copied Workspace. It adds the required delay in 10 ms intervals and checks if the build was cancelled. | Worker |
| `BuildResult` | Stores the generation number, cancellation status, and completed `ProcessingCore`. | Worker / GUI |
| `MainWindow::poll_build()` | Uses a `QTimer` every 50 ms to check if the build finished. It gets the result and decides whether to keep it, discard it, or show an error. | GUI |
| `MainWindow::add_document_file()`, `reset_application()` | Updates the Workspace, increases the generation number, and cancels any running build. | GUI |
| `MainWindow::~MainWindow()` | Cancels the running build and waits for it to finish before the window is destroyed. | GUI |
| Widgets | All widget updates happen on the GUI thread. | GUI |

The worker uses a copy of the Workspace instead of accessing the original one. It also receives the generation number, delay, and cancellation flag. The flag is a `std::atomic<bool>`, so both threads can safely read and write it without a data race. It is stored in a `std::shared_ptr` so it stays alive as long as either thread is still using it. This is the only shared data both threads access during the build.

## 2. Corpus States and Transitions

I will use `enum class CorpusState { NotBuilt, Building, Current, Failed };` to track the corpus state. Search, Inspect, and Build Context are only enabled when the corpus is Current. Build Corpus is disabled while Building so only one build can run at a time.

| Current State | Event | New State | What the User Sees |
|---|---|---|---|
| NotBuilt / Current / Failed | Build Corpus clicked | Building | "Building corpus..." and outputs cleared |
| Building | Build finishes and generation matches | Current | "Corpus: N chunks" |
| Building | Result is cancelled or outdated | NotBuilt | "Corpus: Out of date" |
| Building | Build fails and generation matches | Failed | "Build failed: reason" |
| Building | Build fails and generation differs | NotBuilt | "Corpus: Out of date" |
| Building | Add Document clicked | Building | New document appears and build is cancelled |
| Current | Add Document clicked | NotBuilt | "Corpus: Out of date" and outputs cleared |
| Any state | Reset clicked | NotBuilt | Application returns to its original state |

The generation number is used to check if the Workspace changed during the build. Every time a document is added or Reset is clicked, the generation increases. When the build finishes, the program compares the result's generation with the current generation. If they do not match, the result is discarded. This also applies to build failures, since an error from an outdated build should not change the state to Failed.

## 3. Threatening Sequences

| Sequence | Requirement | How It Is Handled |
|---|---|---|
| Add Document during build | R2 | Generation increases and the old result is discarded. |
| Reset during build | R2 | Generation increases and the running build is cancelled. |
| Build clicked twice | R2, R4 | Build is disabled while Building, so only one build runs. |
| Build fails | R3 | The exception is passed through the future. If the generation matches, the state becomes Failed, an error is displayed, and Build Corpus is enabled again. |
| Exit during build | R4 | The destructor cancels the build and waits for the worker to finish. |
| Repeated builds | R4 | Each future is collected using `get()` before another build starts. |

### Walkthrough A: Adding a Document During a Build

1. The Workspace contains A and B, and the generation is 4. The user clicks Build, so the program copies A and B and starts building with generation 4.
2. The worker starts building and checks the cancellation flag during the delay.
3. The user adds C. The generation increases to 5, the cancel flag is set, and C appears in the list. The state stays Building.
4. The worker sees the cancellation flag and returns a cancelled result with generation 4.
5. `poll_build()` compares generation 4 with 5. Since they do not match, the result is discarded. The state becomes NotBuilt, the status shows "Out of date", and Build is enabled again.

### Walkthrough B: Exiting During a Build

1. The user starts a build with a delay of 5000 ms per document.
2. While the build is running, the user clicks Exit and confirms they want to close the application.
3. The `MainWindow` destructor sets the cancellation flag and waits for the worker using `build_future_.wait()`.
4. The worker finishes its current 10 ms delay, checks the flag, and stops the build.
5. The worker finishes, and the application closes. Since the worker never directly accesses `MainWindow`, there are no callbacks trying to access a destroyed window.

## 4. Ownership of the Built Corpus

1. The worker creates a local `ProcessingCore core` and calls `core.rebuild(snapshot)` using the copied Workspace.
2. After the build finishes, the worker returns a `BuildResult` containing the generation, cancellation status, and `std::move(core)`. This moves the corpus into the future.
3. The GUI thread calls `build_future_.get()` inside `poll_build()` to retrieve the result.
4. If the result is valid, the corpus is moved into `processing_core_` using `std::move(result.core)`, replacing the previous corpus.
5. If the result is outdated, it is discarded when the local `BuildResult` is destroyed.

This keeps the worker and GUI from accessing the same corpus at the same time. The corpus is moved instead of copied because `ProcessingCore` is move-only.

## 5. Rejected Alternative

I considered using `QMetaObject::invokeMethod()` with `Qt::QueuedConnection` to send the result from the worker to `MainWindow`. However, this could cause problems if the window closes while the worker is still running. The worker could try to send a call to a destroyed window, and checking with `QPointer` does not completely prevent this.

Instead, I chose a `QTimer` that checks the future every 50 ms. This keeps the worker separate from `MainWindow`, and only the GUI thread handles the result and updates the widgets. The downside is a possible 50 ms delay before detecting that the build finished, but it makes handling the threads and window lifetime simpler.
