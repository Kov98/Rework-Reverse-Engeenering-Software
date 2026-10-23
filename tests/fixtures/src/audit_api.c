__declspec(dllexport) char *strcpy(char *destination, const char *source) {
    char *start = destination;
    while ((*destination++ = *source++)) {}
    return start;
}
