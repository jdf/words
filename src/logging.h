#pragma once

namespace words {

// Sets up Abseil logging. In the browser, log lines go to the JS console
// (console.log/warn/error by severity) instead of stderr, so engine state
// is inspectable in DevTools; on the host they go to stderr as usual.
// Call once, before the first LOG(). Safe to call repeatedly.
void initLogging();

}  // namespace words
