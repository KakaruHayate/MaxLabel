// Resource initialisation — see Resources.h.

#include "Resources.h"

#include <QtGlobal>

// Q_INIT_RESOURCE declares the generated initialiser in whatever namespace it
// is written in, so this has to sit at global scope — not in a namespace, and
// not in an anonymous one either, which is still a namespace.  File scope with
// internal linkage is the shape that works.
static void init_maxlabel_resources() {
    // The name is the .qrc basename: resources.qrc.
    Q_INIT_RESOURCE(resources);
}

namespace maxlabel::ui {

void init_resources() { init_maxlabel_resources(); }

}  // namespace maxlabel::ui
