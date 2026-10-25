// SPDX-License-Identifier: GPL-2.0-only
// Parallel loop over the system's cores for render passes. Kept free of
// system headers that collide with the engine's names.
#pragma once
#include <cstddef>

namespace parallel {
void run(size_t count,void *context,void (*work)(void *,size_t));
template<class Work> void forEach(size_t count,const Work &work) {
    run(count,const_cast<Work *>(&work),[](void *context,size_t index) { (*static_cast<Work *>(context))(index); });
}
}
