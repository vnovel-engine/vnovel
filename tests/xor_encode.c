/* XOR encoder for NScript/PONScript test scripts.
 * Usage: xor_encode input.txt output.dat
 * XORs each byte with 0x84, matching NScripter mode-1 encryption.
 */
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.txt output.dat\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror("fopen input"); return 1; }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("fopen output"); return 1; }
    int c;
    while ((c = fgetc(in)) != EOF)
        fputc(c ^ 0x84, out);
    fclose(in);
    fclose(out);
    return 0;
}
