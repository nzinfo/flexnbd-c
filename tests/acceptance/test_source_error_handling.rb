# encoding: utf-8

require 'test/unit'
require 'environment'


class TestSourceErrorHandling < Test::Unit::TestCase

  def setup
    @old_env = ENV['FLEXNBD_MS_REQUEST_LIMIT_SECS']
    ENV['FLEXNBD_MS_REQUEST_LIMIT_SECS'] = "4.0"

    @env = Environment.new
    @env.writefile1( "f" * 4 )
    @env.serve1
  end


  def teardown
    @env.nbd1.can_die(0)
    @env.cleanup
    ENV['FLEXNBD_MS_REQUEST_LIMIT_SECS'] = @old_env
  end


  def expect_term_during_migration
    @env.nbd1.can_die(1,9)
  end


  def test_failure_to_connect_reported_in_mirror_cmd_response
    stdout, stderr = @env.mirror12_unchecked
    expect_term_during_migration
    assert_match( /failed to connect/, stderr )
  end


  def test_sigterm_after_hello_quits_with_status_of_1
    expect_term_during_migration
    run_fake( "dest/sigterm_after_hello" )
  end


  def test_destination_hangs_after_connect_reports_error_at_source
    run_fake( "dest/hang_after_connect",
              :err => /Remote server failed to respond/ )
  end


  def test_destination_rejects_connection_reports_error_at_source
    run_fake( "dest/reject_acl",
              :err => /Mirror was rejected/ )
  end


  def test_wrong_size_causes_disconnect
    run_fake( "dest/hello_wrong_size",
              :err => /Remote size does not match local size/ )
  end


  def test_wrong_magic_causes_disconnect
    expect_term_during_migration
    run_fake( "dest/hello_wrong_magic",
              :err => /Mirror was rejected/ )
  end


  def test_disconnect_after_hello_causes_retry
    expect_term_during_migration
    run_fake( "dest/close_after_hello",
              :out => /Mirror started/ )
  end


  def test_write_times_out_causes_retry
    expect_term_during_migration
    run_fake( "dest/hang_after_write" )
  end


  def test_rejected_write_causes_retry
    expect_term_during_migration
    run_fake( "dest/error_on_write" )
  end


  def test_disconnect_before_write_reply_causes_retry
    expect_term_during_migration
    run_fake( "dest/close_after_write" )
  end


  def test_bad_write_reply_causes_retry
    expect_term_during_migration
    run_fake( "dest/write_wrong_magic" )
  end


  def test_pre_entrust_disconnect_causes_retry
    expect_term_during_migration
    run_fake( "dest/close_after_writes" )
  end


  def test_cancel_migration
    run_fake( "dest/break_after_hello" )
  end


  private
  def run_fake(name, opts = {})
    @env.run_fake( name, @env.ip, @env.port2, @env.nbd1.ctrl )
    stdout, stderr = @env.mirror12_unchecked
    assert_success
    assert_match( opts[:err], stderr ) if opts[:err]
    assert_match( opts[:out], stdout ) if opts[:out]
    return stdout, stderr
  end

  def assert_success( msg=nil )
    assert @env.fake_reports_success, msg || "Fake failed"
  end


end # class TestSourceErrorHandling
