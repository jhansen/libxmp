#include "test.h"


TEST(test_depack_lha_l1_lzhuff7)
{
	xmp_context c;
	struct xmp_module_info info;
	int ret;

	c = xmp_create_context();
	fail_unless(c != NULL, "can't create context");
	ret = xmp_load_module(c, "data/l1_lzhuff7");
	fail_unless(ret == 0, "can't load module");

	xmp_start_player(c, 44100, 0);
	xmp_get_module_info(c, &info);

	ret = compare_md5(info.md5, "c993a848f57227660f8b10db1d4d874f");
	fail_unless(ret == 0, "MD5 error");

	xmp_end_player(c);
	xmp_release_module(c);
	xmp_free_context(c);
}
END_TEST
