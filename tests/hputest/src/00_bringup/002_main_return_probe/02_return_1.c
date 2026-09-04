#include <hpu/result.h>
/*
 * Nexus-AM/VCS return-code probe.
 *
 * This testcase intentionally performs no HPU operation.  It isolates the
 * simulator-visible result produced when main() returns nonzero.
 */
int main(void) {
    case_start(__FILE__);
  case_expected_failure(__FILE__);
  return 1;
}
