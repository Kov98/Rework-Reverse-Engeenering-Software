/*
 * Future optional link stubs for decompiler_stage020.c.
 * Not compiled or executed during stage 020.
 * Intentionally model an external call that MAY return; no noreturn
 * annotation is present and no absence-of-effects claim is made.
 */
volatile int rw20_external_counter = 0;

int rw20_unknown_external(int input) {
    ++rw20_external_counter;
    return input + 17;
}

void rw20_may_not_return(void) {
    ++rw20_external_counter;
    /* This implementation returns. Another future fixture variant can
       supply a nonreturning implementation; decompiler inference must
       depend on binary evidence, not the function name. */
}
