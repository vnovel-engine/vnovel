// fate_xp3dec.c — Native KiriKiri SDL2 plugin for Fate/Stay Night XP3 decryption.
//
// Fate's data.xp3 uses XOR 0x36 encryption. Other archives (image.xp3, etc.)
// are NOT encrypted. The filter checks if the data looks encrypted before
// applying decryption: if the first 8 bytes at offset 0 don't form a known
// file header when XOR'd with 0x36, the data is likely already decrypted.
//
// Build: gcc -shared -fPIC -o fate_xp3dec.so fate_xp3dec.c
// Place as cxdec.tpm in the game directory.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#pragma pack(push, 4)
typedef struct {
    uint32_t SizeOfSelf;
    uint64_t Offset;
    void *Buffer;
    uint32_t BufferSize;
    uint32_t FileHash;
} tTVPXP3ExtractionFilterInfo;
#pragma pack(pop)

typedef void (*tTVPXP3ArchiveExtractionFilter)(tTVPXP3ExtractionFilterInfo *info);

/* Check if applying XOR 0x36 would produce valid data.
 * We check the first 8 bytes: if XOR 0x36 produces common file headers
 * (TLG, PNG, JPEG, OGG, WAV, etc.), the data is encrypted.
 * If the raw data already has these headers, it's not encrypted. */
static int is_encrypted(const uint8_t *buf, uint32_t size, uint64_t offset)
{
    if (offset != 0 || size < 4)
        return 0;

    /* Check if raw data starts with known headers (not encrypted) */
    if (size >= 4 && memcmp(buf, "TLG", 3) == 0) return 0;  /* TLG5/TLG6 */
    if (size >= 4 && memcmp(buf, "\x89PNG", 4) == 0) return 0;  /* PNG */
    if (size >= 3 && memcmp(buf, "\xff\xd8\xff", 3) == 0) return 0;  /* JPEG */
    if (size >= 4 && memcmp(buf, "OggS", 4) == 0) return 0;  /* OGG */
    if (size >= 4 && memcmp(buf, "RIFF", 4) == 0) return 0;  /* WAV */
    if (size >= 4 && memcmp(buf, "ID3\x03", 4) == 0) return 0;  /* MP3 */
    if (size >= 2 && memcmp(buf, "//", 2) == 0) return 0;  /* TJS script */
    if (size >= 2 && memcmp(buf, "/*", 2) == 0) return 0;  /* TJS script */
    if (size >= 2 && memcmp(buf, "@i", 2) == 0) return 0;  /* TJS script */
    if (size >= 1 && (buf[0] == '\n' || buf[0] == '\r' || buf[0] == ' ' || buf[0] == '\t')) return 0;

    /* Check if XOR 0x36 produces known headers (encrypted) */
    uint8_t xored[4];
    for (int i = 0; i < 4 && i < (int)size; i++)
        xored[i] = buf[i] ^ 0x36;

    if (size >= 4 && memcmp(xored, "TLG", 3) == 0) return 1;
    if (size >= 4 && memcmp(xored, "\x89PNG", 4) == 0) return 1;
    if (size >= 4 && memcmp(xored, "OggS", 4) == 0) return 1;
    if (size >= 4 && memcmp(xored, "RIFF", 4) == 0) return 1;
    if (size >= 2 && memcmp(xored, "//", 2) == 0) return 1;
    if (size >= 2 && memcmp(xored, "/*", 2) == 0) return 1;
    if (size >= 2 && memcmp(xored, "@i", 2) == 0) return 1;

    /* For text files: check if XOR 0x36 produces printable ASCII */
    int printable_raw = 0, printable_xor = 0;
    for (uint32_t i = 0; i < size && i < 32; i++) {
        if ((buf[i] >= 0x20 && buf[i] < 0x7f) || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t')
            printable_raw++;
        uint8_t x = buf[i] ^ 0x36;
        if ((x >= 0x20 && x < 0x7f) || x == '\n' || x == '\r' || x == '\t')
            printable_xor++;
    }
    /* If XOR'd version is more printable, it's likely encrypted */
    if (printable_xor > printable_raw + 4)
        return 1;
    if (printable_raw > printable_xor + 4)
        return 0;

    /* Ambiguous: apply decryption to be safe (data.xp3 is encrypted) */
    return 1;
}

static void fate_xp3_filter(tTVPXP3ExtractionFilterInfo *info)
{
    uint8_t *buf = (uint8_t *)info->Buffer;

    fprintf(stderr, "filter: off=%llu size=%u hash=0x%x first4=%02x%02x%02x%02x\n",
            (unsigned long long)info->Offset, info->BufferSize, info->FileHash,
            buf[0], buf[1], buf[2], buf[3]);

    /* XOR every byte with 0x36 */
    for (uint32_t i = 0; i < info->BufferSize; i++)
        buf[i] ^= 0x36;
    /* Bit flip at offset 0x13 */
    if (info->Offset <= 0x13 && info->Offset + info->BufferSize > 0x13)
        buf[0x13 - info->Offset] ^= 1;

    /* Bit flip at offset 0x2ea29 */
    if (info->Offset <= 0x2ea29 && info->Offset + info->BufferSize > 0x2ea29)
        buf[0x2ea29 - info->Offset] ^= 3;
}

__attribute__((visibility("default")))
int V2Link(void *exporter)
{
    extern void TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter);
    TVPSetXP3ArchiveExtractionFilter(fate_xp3_filter);
    return 0;
}

__attribute__((visibility("default")))
int V2Unlink(void)
{
    extern void TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter);
    TVPSetXP3ArchiveExtractionFilter(NULL);
    return 0;
}
