#include <check.h>

#include "bitset.h"

START_TEST(test_bit_set)
{
	uint64_t num = 0;
	char *bits = (char*) &num;
	
#define TEST_BIT_SET(bit, newvalue) \
	bit_set(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));
	
	TEST_BIT_SET(0, 1);
	TEST_BIT_SET(1, 3);
	TEST_BIT_SET(2, 7);
	TEST_BIT_SET(7, 0x87);
	TEST_BIT_SET(63, 0x8000000000000087);
}
END_TEST

START_TEST(test_bit_clear)
{
	uint64_t num = 0xffffffffffffffff;
	char *bits = (char*) &num;
	
#define TEST_BIT_CLEAR(bit, newvalue) \
	bit_clear(bits, (bit)); \
	fail_unless(num == (newvalue), "num was %x instead of %x", num, (newvalue));
	
	TEST_BIT_CLEAR(0, 0xfffffffffffffffe);
	TEST_BIT_CLEAR(1, 0xfffffffffffffffc);
	TEST_BIT_CLEAR(2, 0xfffffffffffffff8);
	TEST_BIT_CLEAR(7, 0xffffffffffffff78);
	TEST_BIT_CLEAR(63,0x7fffffffffffff78);
}
END_TEST

START_TEST(test_bit_tests)
{
	uint64_t num = 0x5555555555555555;
	char *bits = (char*) &num;
	
	fail_unless(bit_has_value(bits, 0, 1), "bit_has_value malfunction");
	fail_unless(bit_has_value(bits, 1, 0), "bit_has_value malfunction");
	fail_unless(bit_has_value(bits, 63, 0), "bit_has_value malfunction");
	fail_unless(bit_is_set(bits, 0), "bit_is_set malfunction");
	fail_unless(bit_is_clear(bits, 1), "bit_is_clear malfunction");
	fail_unless(bit_is_set(bits, 62), "bit_is_set malfunction");
	fail_unless(bit_is_clear(bits, 63), "bit_is_clear malfunction");
}
END_TEST

START_TEST(test_bit_ranges)
{
	char buffer[4160];
	uint64_t  *longs = (unsigned long*) buffer;
	uint64_t i;
	
	memset(buffer, 0, 4160);
	
	for (i=0; i<64; i++) {
		bit_set_range(buffer, i*64, i);
		fail_unless(
			longs[i] == (1UL<<i)-1,
			"longs[%ld] = %lx SHOULD BE %lx",
			i, longs[i], (1L<<i)-1
		);
		
		fail_unless(longs[i+1] == 0, "bit_set_range overshot at i=%d", i);
	}
	
	for (i=0; i<64; i++) {
		bit_clear_range(buffer, i*64, i);
		fail_unless(longs[i] == 0, "bit_clear_range didn't work at i=%d", i);
	}
}
END_TEST

START_TEST(test_bit_runs)
{
	char buffer[256];
	int i, ptr=0, runs[] = {
		56,97,22,12,83,1,45,80,85,51,64,40,63,67,75,64,94,81,79,62
	};
	
	memset(buffer,0,256);
	
	for (i=0; i < 20; i += 2) {
		ptr += runs[i];
		bit_set_range(buffer, ptr, runs[i+1]);
		ptr += runs[i+1];
	}
	
	ptr = 0;
	
	for (i=0; i < 20; i += 1) {
		int run = bit_run_count(buffer, ptr, 2048-ptr);
		fail_unless(
			run == runs[i],
			"run %d should have been %d, was %d",
			i, runs[i], run
		);
		ptr += runs[i];
	}
}
END_TEST

START_TEST(test_bitset)
{
	struct bitset_mapping* map;
	uint64_t *num;
	
	map = bitset_alloc(6400, 100);
	num = (uint64_t*) map->bits;
	
	bitset_set_range(map,0,50);
	ck_assert_int_eq(1, *num);
	bitset_set_range(map,99,1);
	ck_assert_int_eq(1, *num);
	bitset_set_range(map,100,1);
	ck_assert_int_eq(3, *num);
	bitset_set_range(map,0,800);
	ck_assert_int_eq(255, *num);
	bitset_set_range(map,1499,2);
	ck_assert_int_eq(0xc0ff, *num);
	bitset_clear_range(map,1499,2);
	ck_assert_int_eq(255, *num);
	
	*num = 0;
	bitset_set_range(map, 1499, 2);
	bitset_clear_range(map, 1300, 200);
	ck_assert_int_eq(0x8000, *num);
	
	*num = 0;
	bitset_set_range(map, 0, 6400);
	ck_assert_int_eq(0xffffffffffffffff, *num);
	bitset_clear_range(map, 3200, 400);
	ck_assert_int_eq(0xfffffff0ffffffff, *num);
}
END_TEST


START_TEST( test_bitset_set )
{
	struct bitset_mapping* map;
	uint64_t *num;
	
	map = bitset_alloc(64, 1);
	num = (uint64_t*) map->bits;

	ck_assert_int_eq( 0x0000000000000000, *num );
	bitset_set( map );
	ck_assert_int_eq( 0xffffffffffffffff, *num );
}
END_TEST


START_TEST( test_bitset_clear )
{
	struct bitset_mapping* map;
	uint64_t *num;
	
	map = bitset_alloc(64, 1);
	num = (uint64_t*) map->bits;

	ck_assert_int_eq( 0x0000000000000000, *num );
	bitset_set( map );
	bitset_clear( map );
	ck_assert_int_eq( 0x0000000000000000, *num );
}
END_TEST


Suite* bitset_suite(void)
{
	Suite *s = suite_create("bitset");
	TCase *tc_bit = tcase_create("bit");
	TCase *tc_bitset = tcase_create("bitset");
	tcase_add_test(tc_bit, test_bit_set);
	tcase_add_test(tc_bit, test_bit_clear);
	tcase_add_test(tc_bit, test_bit_tests);
	tcase_add_test(tc_bit, test_bit_ranges);
	tcase_add_test(tc_bit, test_bit_runs);
	tcase_add_test(tc_bitset, test_bitset);
	tcase_add_test(tc_bitset, test_bitset_set);
	tcase_add_test(tc_bitset, test_bitset_clear);
	suite_add_tcase(s, tc_bit);
	suite_add_tcase(s, tc_bitset);
	
	return s;
}

int main(void)
{
	int number_failed;
	Suite *s = bitset_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}
