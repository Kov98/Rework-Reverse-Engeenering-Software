#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
__declspec(dllimport) char *strcpy(char *destination, const char *source);
#else
#define EXPORT __attribute__((visibility("default")))
static char *strcpy(char *destination, const char *source) {
    char *start = destination;
    while ((*destination++ = *source++)) {}
    return start;
}
#endif

volatile int observed;
char output[64];
const char *message = "reworkbench fixture";

EXPORT __attribute__((noinline)) int compute(int value) {
    if (value > 4) return value * 3 + 7;
    return value - 1;
}

EXPORT int entry(void) {
    strcpy(output, message);
    observed = compute(9);
    return observed;
}
