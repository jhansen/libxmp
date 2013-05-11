#include "test.h"

TEST(test_write_file_32bit_big_endian)
{
	FILE *f;
	int x;

	f = fopen("write_test", "wb");
	fail_unless(f != NULL, "can't open data file");

	write32b(f, 0x12345678);
	fclose(f);

	f = fopen("write_test", "rb");
	x = read32b(f);
	fail_unless(x == 0x12345678, "read error");

	fclose(f);
}
END_TEST
