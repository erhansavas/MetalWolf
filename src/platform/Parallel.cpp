// SPDX-License-Identifier: GPL-2.0-only
#include "Parallel.hpp"
#include <dispatch/dispatch.h>

void parallel::run(size_t count,void *context,void (*work)(void *,size_t)) {
    dispatch_apply_f(count,dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE,0),context,work);
}
