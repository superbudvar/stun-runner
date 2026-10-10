/* dis68k - standalone MC68010 disassembler over a flat ROM image.
 *
 * The shell's STUNRUN_DISASM path can only disassemble once it has booted the
 * CPU, which costs 600 frames per query.  During ROM archaeology we want to
 * disassemble arbitrary ranges with no emulation at all, so this tool links
 * musashi's disassembler (third_party/musashi/m68kdasm.c) directly and feeds
 * it bytes from a flat image.
 *
 * Usage:
 *   dis68k <image> <start_hex> [count] [base_hex]
 *
 * <image>    flat big-endian binary (e.g. the de-interleaved 1 MiB maincpu image)
 * <start>    address to disassemble from, hex, 0x-prefixed
 * <count>    number of instructions (default 40)
 * <base>     address the image is mapped at (default 0)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* musashi supplies the decoding; we supply the memory. */
unsigned int m68k_read_disassembler_16(unsigned int address);
unsigned int m68k_read_disassembler_32(unsigned int address);
unsigned int m68k_disassemble(char *output, unsigned int pc, unsigned int cpu_type);

static uint8_t *g_image;
static size_t g_size;
static uint32_t g_base;

unsigned int m68k_read_disassembler_16(unsigned int address)
{
	size_t off = (size_t)(address - g_base);
	if (off + 1u >= g_size)
		return 0xffffu;
	return (unsigned int)((g_image[off] << 8) | g_image[off + 1]);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
	size_t off = (size_t)(address - g_base);
	if (off + 3u >= g_size)
		return 0xffffffffu;
	return (unsigned int)((g_image[off] << 24) | (g_image[off + 1] << 16) |
	                      (g_image[off + 2] << 8) | g_image[off + 3]);
}


int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr,
		        "usage: %s <image> <start_hex> [count] [base_hex]\n", argv[0]);
		return 2;
	}
	const char *path = argv[1];
	uint32_t pc = (uint32_t)strtoul(argv[2], NULL, 16);
	unsigned count = (argc > 3) ? (unsigned)strtoul(argv[3], NULL, 10) : 40u;
	g_base = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 16) : 0u;

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "dis68k: cannot open %s\n", path);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	g_size = (len > 0) ? (size_t)len : 0u;
	g_image = (g_size > 0) ? (uint8_t *)malloc(g_size) : NULL;
	if (!g_image || (g_size > 0 && fread(g_image, 1, g_size, f) != g_size)) {
		fprintf(stderr, "dis68k: cannot read %s\n", path);
		fclose(f);
		return 1;
	}
	fclose(f);

	/* Mirrors the M68K_CPU_TYPE_* enum in third_party/musashi/m68k.h:96-106,
	 * which starts at 0 = M68K_CPU_TYPE_INVALID, so 68010 is 2.  Passing
	 * anything else makes m68k_disassemble return 0 with an empty string. */
	const unsigned int cpu_type = 2u;
	static const char hx[] = "0123456789abcdef";

	printf("; %s base=%#010x size=%lu\n", path, g_base, (unsigned long)g_size);
	for (unsigned i = 0; i < count; i++) {
		char line[256];
		memset(line, 0, sizeof line);
		/* musashi returns the encoded length, which is what keeps multi-word
		 * 68010 encodings aligned; a zero means the word did not decode. */
		unsigned len = m68k_disassemble(line, pc, cpu_type);
		if (len < 2u)
			len = 2u;

		char bytes[48];
		size_t bi = 0;
		for (unsigned k = 0; k < len && bi + 4u < sizeof bytes; k++) {
			size_t off = (size_t)(pc - g_base) + k;
			uint8_t b = (off < g_size) ? g_image[off] : 0u;
			bytes[bi++] = hx[b >> 4];
			bytes[bi++] = hx[b & 0xf];
			bytes[bi++] = (k + 1u < len) ? ' ' : '\0';
		}
		printf("%08x: %-*s %s\n", pc, (int)(len * 3u), bytes, line);
		pc += len;
	}
	free(g_image);
	return 0;
}