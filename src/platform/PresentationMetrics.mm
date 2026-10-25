// SPDX-License-Identifier: GPL-2.0-only
#include "PresentationMetrics.hpp"
#import <QuartzCore/QuartzCore.h>
namespace { double renderBegin=0,renderTotal=0; }
void Native_MetricsRenderBegin(void) { renderBegin=CACurrentMediaTime(); }
void Native_MetricsRenderEnd(void) { renderTotal+=(CACurrentMediaTime()-renderBegin)*1000; }
double Native_MetricsTakeRenderMs() { double total=renderTotal; renderTotal=0; return total; }
