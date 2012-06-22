#include "status.h"
#include "serve.h"
#include "ioutil.h"
#include "util.h"

#include <check.h>

START_TEST( test_status_create )
{
	struct server server;
	struct status *status = NULL;

	status = status_create( &server );

	fail_if( NULL == status, "Status wasn't allocated" );
	status_destroy( status );
}
END_TEST

START_TEST( test_gets_has_control )
{
	struct server server;
	struct status * status;

	server.has_control = 1;
	status = status_create( &server );

	fail_unless( status->has_control == 1, "has_control wasn't copied" );
	status_destroy( status );
}
END_TEST


START_TEST( test_gets_is_mirroring )
{
	struct server server;
	struct status * status;

	server.mirror = NULL;
	status = status_create( &server );
	fail_if( status->is_mirroring, "is_mirroring was set" );
	status_destroy( status );

	server.mirror = (struct mirror_status *)xmalloc( sizeof( struct mirror_status ) );
	status = status_create( &server );
	fail_unless( status->is_mirroring, "is_mirroring wasn't set" );
	status_destroy( status );
}
END_TEST


START_TEST( test_renders_has_control )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.has_control = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );

	char *found = strstr( buf, "has_control=true" );
	fail_if( NULL == found, "has_control=true not found" );

	status.has_control = 0;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "has_control=false" );
	fail_if( NULL == found, "has_control=false not found" );

}
END_TEST


START_TEST( test_renders_is_mirroring )
{
	struct status status;
	int fds[2];
	pipe(fds);
	char buf[1024] = {0};

	status.is_mirroring = 1;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );

	char *found = strstr( buf, "is_mirroring=true" );
	fail_if( NULL == found, "is_mirroring=true not found" );

	status.is_mirroring = 0;
	status_write( &status, fds[1] );

	fail_unless( read_until_newline( fds[0], buf, 1024 ) > 0,
			"Couldn't read the result" );
	found = strstr( buf, "is_mirroring=false" );
	fail_if( NULL == found, "is_mirroring=false not found" );

}
END_TEST


Suite *status_suite(void)
{
	Suite *s = suite_create("status");
	TCase *tc_create = tcase_create("create");
	TCase *tc_render = tcase_create("render");

	tcase_add_test(tc_create, test_status_create);
	tcase_add_test(tc_create, test_gets_has_control);
	tcase_add_test(tc_create, test_gets_is_mirroring);

	tcase_add_test(tc_render, test_renders_has_control);
	tcase_add_test(tc_render, test_renders_is_mirroring);

	suite_add_tcase(s, tc_create);
	suite_add_tcase(s, tc_render);

	return s;
}

int main(void)
{
	int number_failed;
	
	Suite *s = status_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? 0 : 1;
}


