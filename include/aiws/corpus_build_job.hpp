#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <functional>
#include <memory>

namespace aiws {

/** @brief What a finished background build hands back to the GUI thread. */
struct BuildResult {
    bool cancelled{false};
    ProcessingCore core;
};

/** @brief Makes the ProcessingCore a build uses. Runs on the worker thread. */
using CoreFactory = std::function<ProcessingCore()>;

/**
 * @brief Builds a corpus from a private copy of the workspace. Runs on the worker.
 *
 * Touches no Qt object and no MainWindow member. The only data it shares with
 * the GUI thread is the atomic cancel flag. An exception thrown by the build
 * travels to the GUI thread through the std::future.
 */
BuildResult run_corpus_build(Workspace snapshot,
                             int delay_ms_per_document,
                             std::shared_ptr<std::atomic<bool>> cancel,
                             CoreFactory make_core);

}  // namespace aiws
