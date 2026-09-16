#include "design_select.h"
#include "route.h"
#include "orbit.h"

int main(void) {
#if LONETRAIL_USE_ORBIT
  lonetrail_run(&ORBIT_DESIGN);
#else
  lonetrail_run(&ROUTE_DESIGN);
#endif
}
