/* Freestanding Ripes driver for ida.c: the gcc reference build that the
 * hand-written rubik.s is measured against. It does the same work per case
 * as rubik.s: parse, solve, print the moves, replay them to check (T5).
 *   riscv64-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib
 */
#define NO_MAIN
#include "ida.c"

#ifndef STATE
#define STATE "21345671111111"
#endif

static void ecall1(int number, uintptr_t arg)
{
    register uintptr_t a0 __asm__("a0") = arg;
    register int a7 __asm__("a7") = number;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static const char move_names[9][4] = {"R",  "R2", "R'", "B", "B2",
                                      "B'", "D",  "D2", "D'"};

/* Replays the path through the quarter-turn tables; 1 if it reaches solved. */
static int replay(uint16_t p, uint16_t o, const uint8_t *path, int length)
{
    for (int i = 0; i < length; ++i) {
        uint8_t face = 0, turns = path[i];
        while (turns >= 3) {
            turns -= 3;
            ++face;
        }
        for (uint8_t t = 0; t <= turns; ++t) {
            p = perm_turn[face][p];
            o = orient_turn[face][o];
        }
    }
    return !(p | o);
}

void _start(void)
{
    static const char input[] = STATE;
    uint16_t p, o;
    uint8_t path[MAX_DEPTH];
    int status = 2;
    ecall1(4, (uintptr_t) input);
    if (parse(input, &p, &o)) {
        int length = solve(p, o, path);
        ecall1(4, (uintptr_t) " -> ");
        for (int i = 0; i < length; ++i) {
            if (i)
                ecall1(11, ' ');
            ecall1(4, (uintptr_t) move_names[path[i]]);
        }
        status = !replay(p, o, path, length);
    }
    ecall1(93, (uintptr_t) status);
    for (;;)
        ;
}
