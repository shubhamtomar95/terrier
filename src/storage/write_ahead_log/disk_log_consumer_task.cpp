#include "storage/write_ahead_log/disk_log_consumer_task.h"
#include "common/timer.h"
#include "common/thread_context.h"
#include "metrics/metrics_store.h"

namespace terrier::storage {

void DiskLogConsumerTask::RunTask() {
  run_task_ = true;
  DiskLogConsumerTaskLoop();
}

void DiskLogConsumerTask::Terminate() {
  // If the task hasn't run yet, yield the thread until it's started
  while (!run_task_) std::this_thread::yield();
  TERRIER_ASSERT(run_task_, "Cant terminate a task that isnt running");
  // Signal to terminate and force a flush so task persists before LogManager closes buffers
  run_task_ = false;
  disk_log_writer_thread_cv_.notify_one();
}

void DiskLogConsumerTask::WriteBuffersToLogFile() {
  // Persist all the filled buffers to the disk
  SerializedLogs logs;
  while (!filled_buffer_queue_->Empty()) {
    // Dequeue filled buffers and flush them to disk, as well as storing commit callbacks
    filled_buffer_queue_->Dequeue(&logs);
    current_data_written_ += logs.first->FlushBuffer();
    commit_callbacks_.insert(commit_callbacks_.end(), logs.second.begin(), logs.second.end());
    // Enqueue the flushed buffer to the empty buffer queue
    empty_buffer_queue_->Enqueue(logs.first);
  }
}

uint64_t DiskLogConsumerTask::PersistLogFile() {
  TERRIER_ASSERT(!buffers_->empty(), "Buffers vector should not be empty until Shutdown");
  // Force the buffers to be written to disk. Because all buffers log to the same file, it suffices to call persist on
  // any buffer.
  buffers_->front().Persist();
  const auto num_buffers = commit_callbacks_.size();
  // Execute the callbacks for the transactions that have been persisted
  for (auto &callback : commit_callbacks_) callback.first(callback.second);
  commit_callbacks_.clear();
  return num_buffers;
}

void DiskLogConsumerTask::DiskLogConsumerTaskLoop() {
  uint64_t write_us = 0, persist_us = 0, num_bytes = 0, num_buffers = 0;
  // Keeps track of how much data we've written to the log file since the last persist
  current_data_written_ = 0;
  // Time since last log file persist
  auto last_persist = std::chrono::high_resolution_clock::now();
  // Disk log consumer task thread spins in this loop. When notified or periodically, we wake up and process serialized
  // buffers
  do {
    {
      // Wait until we are told to flush buffers
      std::unique_lock<std::mutex> lock(persist_lock_);
      // Wake up the task thread if:
      // 1) The serializer thread has signalled to persist all non-empty buffers to disk
      // 2) There is a filled buffer to write to the disk
      // 3) LogManager has shut down the task
      // 4) Our persist interval timed out
      disk_log_writer_thread_cv_.wait_for(lock, persist_interval_,
                                          [&] { return do_persist_ || !filled_buffer_queue_->Empty() || !run_task_; });
    }

    double elapsed_us = 0;
    {
      common::ScopedTimer<std::chrono::microseconds> scoped_timer(&elapsed_us);
      // Flush all the buffers to the log file
      WriteBuffersToLogFile();
    }
    write_us += elapsed_us;

    // We persist the log file if the following conditions are met
    // 1) The persist interval amount of time has passed since the last persist
    // 2) We have written more data since the last persist than the threshold
    // 3) We are signaled to persist
    // 4) We are shutting down this task
    bool timeout = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() -
                                                                         last_persist) > persist_interval_;
    if (timeout || current_data_written_ > persist_threshold_ || do_persist_ || !run_task_) {
      common::ScopedTimer<std::chrono::microseconds> scoped_timer(&elapsed_us);
      {
        std::unique_lock<std::mutex> lock(persist_lock_);
        num_buffers = PersistLogFile();
        num_bytes = current_data_written_;
        // Reset meta data
        last_persist = std::chrono::high_resolution_clock::now();
        current_data_written_ = 0;
        do_persist_ = false;
      }
      // Signal anyone who forced a persist that the persist has finished
      persist_cv_.notify_all();
    }
    persist_us = elapsed_us;

    if (num_bytes > 0 && common::thread_context.metrics_store_ != nullptr &&
        common::thread_context.metrics_store_->ComponentEnabled(metrics::MetricsComponent::LOGGING)) {
      common::thread_context.metrics_store_->RecordConsumerData(write_us, persist_us, num_bytes, num_buffers);
      write_us = persist_us = num_bytes = num_buffers = 0;
    }
  } while (run_task_);
  // Be extra sure we processed everything
  WriteBuffersToLogFile();
  PersistLogFile();
}
}  // namespace terrier::storage
