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

/* Replays the path on the three views through the quarter-turn tables; 1 if
 * all of them reach the solved key.
 */
static int replay(const uint16_t *vb, const uint16_t *vo, const uint8_t *path,
                  int length)
{
    uint16_t b[VIEWS], o[VIEWS];
    int changed = 0;
    for (int k = 0; k < VIEWS; ++k) {
        b[k] = vb[k];
        o[k] = vo[k];
    }
    for (int i = 0; i < length; ++i) {
        uint8_t face = 0, turns = path[i];
        while (turns >= 3) {
            turns -= 3;
            ++face;
        }
        for (uint8_t t = 0; t <= turns; ++t)
            for (uint8_t k = 0, f = face; k < VIEWS; ++k, f = sym_face[f]) {
                b[k] = block_turn[f][b[k]];
                o[k] = orient_turn[f][o[k]];
            }
    }
    for (int k = 0; k < VIEWS; ++k)
        changed |= (b[k] ^ BLOCK_SOLVED) | o[k];
    return !changed;
}

void _start(void)
{
    /* gcc puts the small tables in .sdata and the linker reaches them
     * through gp, which nothing has set up: Ripes starts at _start */
    __asm__(".option push\n.option norelax\nla gp, __global_pointer$\n"
            ".option pop");
    static const char input[] = STATE;
    uint16_t vb[VIEWS], vo[VIEWS];
    uint8_t parity, path[MAX_DEPTH];
    int status = 2;
    ecall1(4, (uintptr_t) input);
    if (parse(input, &parity)) {
        views(input, parity, vb, vo);
        int length = solve(vb, vo, path);
        ecall1(4, (uintptr_t) " -> ");
        for (int i = 0; i < length; ++i) {
            if (i)
                ecall1(11, ' ');
            ecall1(4, (uintptr_t) move_names[path[i]]);
        }
        status = !replay(vb, vo, path, length);
    }
    ecall1(93, (uintptr_t) status);
    for (;;)
        ;
}
