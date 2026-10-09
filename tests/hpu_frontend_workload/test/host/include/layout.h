#ifndef FRONTEND_LAYOUT_H
#define FRONTEND_LAYOUT_H
#define CASE_ID "host-runtime-test"
#define DEGREE 64U
#define WINDOW_LINES 2U
#define IMAGE_LINES 3U
#define GOLDEN_WORDS 64U
#define OUTPUT_COUNT 1U
struct output_span { unsigned line, words, golden_word, modulus, step, component, modulus_id; };
extern const struct output_span outputs[OUTPUT_COUNT];
#endif
