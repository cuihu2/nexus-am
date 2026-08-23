/*
 * Nexus-AM/VCS return-code probe.
 *
 * This testcase intentionally performs no HPU operation.  It isolates the
 * simulator-visible result produced when main() returns nonzero.
 */
int main(void) {
  return 1;
}
