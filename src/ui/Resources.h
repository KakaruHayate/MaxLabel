#pragma once

// Resource initialisation, which every entry point has to perform.
//
// The theme and the icons live in resources.qrc, which is compiled into the
// maxlabel_ui static library.  A static library's resource initialiser is not
// referenced by anything, so the linker discards it — and the symptom is not a
// crash but an unstyled window with no icons, which nothing but a human
// looking at a screenshot notices.  It happened once.
//
// So it is a named function rather than a macro sprinkled at each entry point:
// calling it is what pulls the resources in, and forgetting to is a thing the
// compiler can at least make visible.

namespace maxlabel::ui {

void init_resources();

}  // namespace maxlabel::ui
