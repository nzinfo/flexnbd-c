require 'socket'
require 'thread'

Thread.abort_on_exception = true

# Noddy test class to exercise FlexNBD from the outside for testing.
#
class FlexNBD
  attr_reader :bin, :ctrl, :pid, :ip, :port

  def initialize(bin, ip, port)
    @bin  = bin
    @debug = `#{@bin} serve --help` =~ /--verbose/ ? "--verbose" : ""
    @valgrind = ENV['VALGRIND'] ? "valgrind " : ""
    @bin = "#{@valgrind}#{@bin}"
    raise "#{bin} not executable" unless File.executable?(bin)
    @ctrl = "/tmp/.flexnbd.ctrl.#{Time.now.to_i}.#{rand}"
    @ip = ip
    @port = port
    @kill = false
  end

  def debug?
    !@debug.empty? || ENV['DEBUG']
  end

  def debug( msg )
    $stderr.puts msg if debug?
  end


  def serve_cmd( file, acl )
    "#{@bin} serve "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--file #{file} "\
      "--sock #{ctrl} "\
      "#{@debug} "\
      "#{acl.join(' ')}"
  end


  def read_cmd( offset, length )
    "#{@bin} read "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--from #{offset} "\
      "#{@debug} "\
      "--size #{length}"
  end


  def write_cmd( offset, data )
    "#{@bin} write "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--from #{offset} "\
      "#{@debug} "\
      "--size #{data.length}"
  end


  def mirror_cmd(dest_ip, dest_port)
    "#{@bin} mirror "\
      "--addr #{dest_ip} "\
      "--port #{dest_port} "\
      "--sock #{ctrl} "\
      "#{@debug} "
  end

  def serve(file, *acl)
    File.unlink(ctrl) if File.exists?(ctrl)
    cmd =serve_cmd( file, acl )
    debug( cmd )

    @pid = fork do exec(cmd) end
    start_wait_thread( @pid )

    while !File.socket?(ctrl)
      pid, status = Process.wait2(@pid, Process::WNOHANG)
      raise "server did not start (#{cmd})" if pid
      sleep 0.1
    end
    at_exit { kill }
  end

  def start_wait_thread( pid )
    Thread.start do
      Process.waitpid2( pid )
      if @kill
        fail "flexnbd quit with a bad status #{$?.exitstatus}" unless 
          $?.exitstatus == @kill
      else
        $stderr.puts "flexnbd quit"
        fail "flexnbd quit early"
      end
    end
  end


  def can_die(status=0)
    @kill = status
  end

  def kill
    can_die()
    begin
      Process.kill("INT", @pid)
    rescue Errno::ESRCH => e
      # already dead.  Presumably this means it went away after a
      # can_die() call.
    end
  end

  def read(offset, length)
    cmd = read_cmd( offset, length )
    debug( cmd )

    IO.popen(cmd) do |fh|
      return fh.read
    end
    raise IOError.new "NBD read failed" unless $?.success?
    out
  end

  def write(offset, data)
    cmd = write_cmd( offset, data )
    debug( cmd )
    
    IO.popen(cmd, "w") do |fh|
      fh.write(data)
    end
    raise IOError.new "NBD write failed" unless $?.success?
    nil
  end

  def mirror(dest_ip, dest_port, bandwidth=nil, action=nil)
    cmd = mirror_cmd( dest_ip, dest_port)
    debug( cmd )
    system cmd
    raise IOError.new( "Migrate command failed") unless $?.success?
    nil
  end

  def acl(*acl)
    control_command("acl", *acl)
  end

  def status
  end

  protected
  def control_command(*args)
    raise "Server not running" unless @pid
    args = args.compact
    UNIXSocket.open(@ctrl) do |u|
      u.write(args.join("\n") + "\n")
      code, message = u.readline.split(": ", 2)
      return [code, message]
    end
  end
end

