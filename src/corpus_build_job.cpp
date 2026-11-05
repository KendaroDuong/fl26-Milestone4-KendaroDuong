#include "aiws/corpus_build_job.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace aiws {

namespace {

constexpr int kDelaySliceMs = 10;

// Sleeps for the requested time in short slices. Returns false if cancelled.
bool delay_unless_cancelled(int delay_ms, const std::atomic<bool>& cancel) {
    for (int waited = 0; waited < delay_ms; waited += kDelaySliceMs) {
        if (cancel.load()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDelaySliceMs));
    }
    return !cancel.load();
}

}  // namespace

BuildResult run_corpus_build(Workspace snapshot,
                             int delay_ms_per_document,
                             std::shared_ptr<std::atomic<bool>> cancel,
                             CoreFactory make_core) {
    BuildResult result;

    for (std::size_t i = 0; i < snapshot.documents().size(); ++i) {
        if (!delay_unless_cancelled(delay_ms_per_document, *cancel)) {
            result.cancelled = true;
            return result;
        }
    }

    ProcessingCore core = make_core ? make_core() : ProcessingCore{};
    core.rebuild(snapshot);
    result.core = std::move(core);
    return result;
}

}  // namespace aiws
