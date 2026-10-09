/* The three root distances of each state given on the command line, as
 * ida.c finds them; used by r16-inverse.py.
 */
#define NO_MAIN
#include "../../ida.c"
#include <stdio.h>
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) { uint16_t vb[3], vo[3]; views(argv[i], vb, vo);
        printf("%s far %d %d %d\n", argv[i], root_dist(vb[0], vo[0]), root_dist(vb[1], vo[1]), root_dist(vb[2], vo[2])); }
    return 0;
}
