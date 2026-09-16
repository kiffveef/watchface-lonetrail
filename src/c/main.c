#include "route.h"
#include "orbit.h"

// scripts/build-all.sh が生成する design_config.h で起動するデザインを切り替える。
// 無ければ(手元の pebble build)Orbit
#if __has_include("design_config.h")
#include "design_config.h"
#endif
#ifndef LONETRAIL_USE_ORBIT
#define LONETRAIL_USE_ORBIT 1
#endif

int main(void) {
  lonetrail_run(LONETRAIL_USE_ORBIT ? &ORBIT_DESIGN : &ROUTE_DESIGN);
}
