#include <hpu/result.h>
/*
 * Nexus-AM/VCS return-code probe.
 *
 * This testcase intentionally performs no HPU operation.  It isolates the
 * simulator-visible result produced when main() returns zero.
 */
int main(void) {
    case_start(__FILE__);
  (void)case_pass(__FILE__);
  return 0;
}
