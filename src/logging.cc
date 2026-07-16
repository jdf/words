#include "logging.h"

#include <absl/log/initialize.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/emscripten.h>

#include <absl/base/log_severity.h>
#include <absl/log/globals.h>
#include <absl/log/log_entry.h>
#include <absl/log/log_sink.h>
#include <absl/log/log_sink_registry.h>

#include <string>
#endif

namespace words {

#ifdef __EMSCRIPTEN__
namespace {

// Forwards each log line to the browser console at a matching level.
class ConsoleSink : public absl::LogSink {
 public:
  void Send(const absl::LogEntry& entry) override {
    std::string message(entry.text_message_with_prefix());
    EM_ASM(
        {
          const msg = UTF8ToString($0);
          if ($1 <= 0) {
            console.log(msg);
          } else if ($1 == 1) {
            console.warn(msg);
          } else {
            console.error(msg);
          }
        },
        message.c_str(), static_cast<int>(entry.log_severity()));
  }
};

}  // namespace
#endif

void initLogging() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;
  absl::InitializeLog();
#ifdef __EMSCRIPTEN__
  static ConsoleSink sink;
  absl::AddLogSink(&sink);
  // The console sink owns browser output; keep stderr (which the page
  // routes to console.error via printErr) from double-printing.
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kFatal);
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
#endif
}

}  // namespace words
