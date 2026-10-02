#pragma once

// Where the tool looks for its data files.
//
// The BudouX models ship with the tool, and the language-detection model will
// too.  A development build points at the source tree; a release points at the
// directory beside the binary.  Both are the same lookup, so nothing else has
// to know which kind of build it is in.

#include <string>

namespace maxlabel {

// The directory data files are looked up in.  Never empty: it falls back to the
// compiled-in default when nothing has been set.
const std::string & model_directory();

// Overrides it — what the command line and the release layout use.
void set_model_directory(std::string directory);

// The directory `argv[0]` lives in, or "" when it cannot be told.  A release
// ships `models/` beside the binary, so this is how the default is found
// without hardcoding an install prefix.
std::string executable_directory(const std::string & argv0);

// Points the model directory at `<executable dir>/models` when that exists.
// A development build keeps the compiled-in default instead, which is the
// source tree.  Returns whether it found one.
bool use_bundled_models(const std::string & argv0);

}  // namespace maxlabel
