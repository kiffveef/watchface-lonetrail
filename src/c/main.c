#include "route.h"
#include "orbit.h"

// 起動するデザイン。2つの .pbw として出す方法は未決定(TODO.md)
#ifndef LONETRAIL_USE_ORBIT
#define LONETRAIL_USE_ORBIT 1
#endif

int main(void) {
  lonetrail_run(LONETRAIL_USE_ORBIT ? &ORBIT_DESIGN : &ROUTE_DESIGN);
}
