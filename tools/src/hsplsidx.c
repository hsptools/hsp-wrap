#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>


int
main (int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Error: Incorrect number of parameters\nUsage: hsplsidx INDEXFILE\n");
  }

  char *fn = argv[1];
  FILE *f  = fopen(fn, "r");

  while (!feof(f)) {
    uint16_t stream;
    uint64_t voffset;
    uint64_t poffset;
    uint64_t count;
    uint32_t crc;

    fread(&stream, sizeof(stream), 1, f);
    fread(&voffset, sizeof(voffset), 1, f);
    fread(&poffset, sizeof(poffset), 1, f);
    fread(&count, sizeof(count), 1, f);
    fread(&crc, sizeof(crc), 1, f);

    printf("Stream: %2"   PRIu16 "  "
	   "VOffset: %10" PRIu64 "  "
	   "POffset: %10" PRIu64 "  "
	   "Size: %10"    PRIu64 "  "
	   "CRC: 0x%"       PRIX32 "\n",
	   stream, voffset, poffset, count, crc);
  }

  return EXIT_SUCCESS;
}
