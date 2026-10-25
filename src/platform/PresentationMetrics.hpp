// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Around each software-rendered view.
extern "C" void Native_MetricsRenderBegin(void);
extern "C" void Native_MetricsRenderEnd(void);
// Software render time since the last call, in milliseconds; it steers the
// adaptive render scale (RenderScale.hpp).
double Native_MetricsTakeRenderMs();
