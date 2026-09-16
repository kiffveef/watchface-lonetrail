#pragma once

// scripts/build-all.sh が生成する design_config.h で起動するデザインを切り替える。
// 無ければ(手元の pebble build)Orbit。選ばれなかったデザインの .c は空になり、
// そのデザイン専用のリソースを package.json から外してもビルドできる
#if __has_include("design_config.h")
#include "design_config.h"
#endif
#ifndef LONETRAIL_USE_ORBIT
#define LONETRAIL_USE_ORBIT 1
#endif
