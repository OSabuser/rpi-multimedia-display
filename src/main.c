#include <stdio.h>

/* Phase 0 stub — main entry point, implemented in Phase 2 */
int main(void)
{
    int var_1 = 0;
    printf("indicator stub 1\n");
    fflush(stdout); /* сброс буфера — нужен когда stdout не TTY */
    printf("indicator stub 2\n");
    fflush(stdout); /* сброс буфера — нужен когда stdout не TTY */
    printf("indicator stub: %d\n", var_1);
    fflush(stdout); /* сброс буфера — нужен когда stdout не TTY */
    return 0;
}
