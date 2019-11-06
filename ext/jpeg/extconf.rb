require 'mkmf'
require 'optparse'
require 'rbconfig'

OptionParser.new { |opt|
  opt.on('--with-jpeg-include=PATH', String) { |path|
    $CFLAGS << " -I#{path}"
  }

  opt.on('--with-jpeg-lib=PATH', String) { |path|
    $LDFLAGS << " -L#{path}"
  }

  opt.parse!(ARGV)
}

have_library( "jpeg")
have_header( "jpeglib.h")

RbConfig::CONFIG.instance_eval {
  flag = false

  if /gcc/ =~ self['CC']
    flag = ['CFLAGS', 'CPPFLAGS'].any? {|s| /-D_FORTIFY_SOURCE/ =~ self[s]}
  end

  have_library( "ssp") if flag
}

create_makefile( "jpeg/jpeg")
