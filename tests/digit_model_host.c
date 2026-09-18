#include "digit_model.h"
#include <stdio.h>
#include <stdint.h>
int main(int argc, char **argv)
{
    uint8_t input[784];
    float scores[10];
    FILE *stream;
    int i;
    if (argc != 2) return 2;
    stream = fopen(argv[1], "rb");
    if (!stream) return 3;
    if (fread(input, 1, sizeof(input), stream) != sizeof(input) ||
        fgetc(stream) != EOF) { fclose(stream); return 4; }
    fclose(stream);
    if (digit_model_infer(input, scores)) return 5;
    for (i = 0; i < 10; ++i) printf("%.9g%c", scores[i], i == 9 ? '\n' : ' ');
    return 0;
}
