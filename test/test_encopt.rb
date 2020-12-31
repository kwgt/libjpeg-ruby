require 'test/unit'
require 'base64'
require 'pathname'
require 'zlib'
require 'jpeg'

class TestEncodeOptions < Test::Unit::TestCase
  DATA_DIR  = Pathname($0).expand_path.dirname + "data"

  def read_raw(file)
    return Zlib::Inflate.inflate((DATA_DIR + file).binread)
  end

  #
  # no option
  #
  test "no option" do
    #
    # specify by symbol
    #
    enc = assert_nothing_raised {
      JPEG::Encoder.new(200, 300)
    }

    assert_kind_of(JPEG::Encoder, enc)
  end

  #
  # number of argument
  #
  test "number of argument" do
    #
    # not enough argument
    #
    assert_raise_kind_of(ArgumentError) {
      JPEG::Encoder.new(200)
    }

    #
    # too many argument
    #
    assert_raise_kind_of(ArgumentError) {
      JPEG::Encoder.new(200, 300, 400)
    }

    assert_raise_kind_of(ArgumentError) {
      JPEG::Encoder.new(200, 300, {})
    }

    #
    # with option
    #
    assert_nothing_raised {
      JPEG::Encoder.new(200, 300, **{})
    }
  end

  #
  # pixel_format (normal)
  #
  data {
    {
      "RGB"       => {:fmt => :RGB,       :ext => ".rgb"},
      "RGB24"     => {:fmt => :RGB24,     :ext => ".rgb"},
      "BGR"       => {:fmt => :BGR,       :ext => ".bgr"},
      "BGR24"     => {:fmt => :BGR24,     :ext => ".bgr"},
      "YUV444"    => {:fmt => :YUV444,    :ext => ".ycbcr"},
      "YCbCr"     => {:fmt => :YCbCr,     :ext => ".ycbcr"},
      "RGBX"      => {:fmt => :RGBX,      :ext => ".rgb32"},
      "RGB32"     => {:fmt => :RGB32,     :ext => ".rgb32"},
      "BGRX"      => {:fmt => :BGRX,      :ext => ".bgr32"},
      "BGR32"     => {:fmt => :BGR32,     :ext => ".bgr32"},
      "GRAYSCALE" => {:fmt => :GRAYSCALE, :ext => ".y"},
    }
  }

  test "pixel_format (normal)" do |info|
    #
    # specify by symbol
    #
    enc  = assert_nothing_raised {
      JPEG::Encoder.new(200, 300, :pixel_format => info[:fmt])
    }

    file = "DSC_0215_small" + info[:ext] + ".zlib"
    raw  = read_raw(file)

    jpg  = assert_nothing_raised {enc << raw}

    if ENV['SAVE']
      Pathname(file).basename.sub_ext(".jpg").binwrite(jpg)
    end
  end

  #
  # pixel_format (invalid value)
  #
  data(
    "Integer" => {:val => 1,      :exc => TypeError},
    "Float"   => {:val => 2.0,    :exc => TypeError},
    "Boolean" => {:val => true,   :exc => TypeError},
    "Array"   => {:val => [],     :exc => TypeError},
    "Symbol"  => {:val => :SRGB,  :exc => ArgumentError},
    "String"  => {:val => "SRGB", :exc => ArgumentError},
  )

  test "pixel_format (invalid value)" do |info|
    assert_raise_kind_of(info[:exc]) {
      JPEG::Encoder.new(200, 300, :pixel_format => info[:val])
    }
  end

  #
  # quality (normal)
  #
  data(
    "0 (Integer)"   => 0,
    "10 (Integer)"  => 10,
    "20 (Integer)"  => 20,
    "30 (Integer)"  => 30,
    "40 (Integer)"  => 40,
    "50 (Integer)"  => 50,
    "60 (Integer)"  => 60,
    "70 (Integer)"  => 70,
    "80 (Integer)"  => 80,
    "90 (Integer)"  => 90,
    "100 (Integer)" => 100,
    "0 (Float)"     => 0.0,
    "10 (Float)"    => 10.0,
    "20 (Float)"    => 20.0,
    "30 (Float)"    => 30.0,
    "40 (Float)"    => 40.0,
    "50 (Float)"    => 50.0,
    "60 (Float)"    => 60.0,
    "70 (Float)"    => 70.0,
    "80 (Float)"    => 80.0,
    "90 (Float)"    => 90.0,
    "100 (Float)"   => 100.0,
  )

  test "quality (normal)" do |q|
    enc  = assert_nothing_raised {
      JPEG::Encoder.new(200, 300, :pixel_format => :YCbCr, :quality => q)
    }

    file = "DSC_0215_small.ycbcr.zlib"
    raw  = read_raw(file)

    jpg  = assert_nothing_raised {enc << raw}

    if ENV['SAVE']
      Pathname(file).basename(".zlib").sub_ext(".#{q}.jpg").binwrite(jpg)
    end
  end

  #
  # quality (invalid value)
  #
  data(
    "String"   => {:val => "ABC",           :exc => TypeError},
    "Array"    => {:val => [],              :exc => TypeError},
    "Symbol"   => {:val => :ABC,            :exc => TypeError},
    "-1"       => {:val => -1,              :exc => RangeError},
    "101"      => {:val => 101,             :exc => RangeError},
    "NaN"      => {:val => Float::NAN,      :exc => ArgumentError},
    "Infinity" => {:val => Float::INFINITY, :exc => ArgumentError},
    "-0.01"    => {:val => -0.01,           :exc => RangeError},
    "101.0"    => {:val => 101.0,           :exc => RangeError},
  )

  test "quality (invalid value)" do |info|
    assert_raise_kind_of(info[:exc]) {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :YCbCr,
                        :quality => info[:val])
    }
  end

  #
  # dct_method (normal)
  #
  data(
    "FASTEST (String)" => {:val => "FASTEST", :label => "str1"},
    "ISLOW (String)"   => {:val => "ISLOW",   :label => "str2"},
    "IFAST (String)"   => {:val => "IFAST",   :label => "str3"},
    "FLOAT (String)"   => {:val => "FLOAT",   :label => "str4"},
    "FASTEST (Symbol)" => {:val => :FASTEST,  :label => "sym1"},
    "ISLOW (Symbol)"   => {:val => :ISLOW,    :label => "sym2"},
    "IFAST (Symbol)"   => {:val => :IFAST,    :label => "sym3"},
    "FLOAT (Symbol)"   => {:val => :FLOAT,    :label => "sym4"},
  )

  test "dct_method (normal)" do |info|
    enc  = assert_nothing_raised {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :YCbCr,
                        :dct_method => info[:val])
    }

    file = "DSC_0215_small.ycbcr.zlib"
    raw  = read_raw(file)

    jpg  = assert_nothing_raised {enc << raw}

    if ENV['SAVE']
      lab  = info[:label]
      Pathname(file).basename(".zlib").sub_ext(".#{lab}.jpg").binwrite(jpg)
    end
  end

  #
  # dct_method (invalid value)
  #
  data(
    "Integer" => {:val => 1,      :exc => TypeError},
    "Float"   => {:val => 2.0,    :exc => TypeError},
    "Boolean" => {:val => true,   :exc => TypeError},
    "Array"   => {:val => [],     :exc => TypeError},
    "Symbol"  => {:val => :XXXX,  :exc => ArgumentError},
    "String"  => {:val => "XXXX", :exc => ArgumentError},
  )

  test "dct_method (invalid value)" do |info|
    assert_raise_kind_of(info[:exc]) {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :YCbCr,
                        :dct_method => info[:val])
    }
  end

  #
  # orientation (normal)
  #
  data(
    "1" => 1,
    "2" => 2,
    "3" => 3,
    "4" => 4,
    "5" => 5,
    "6" => 6,
    "7" => 7,
    "8" => 8,
  )

  test "orientation (normal)" do |o|
    enc  = assert_nothing_raised {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :YCbCr,
                        :orientation => o)
    }

    file = "DSC_0215_small.ycbcr.zlib"
    raw  = read_raw(file)

    jpg  = assert_nothing_raised {enc << raw}

    if ENV['SAVE']
      Pathname(file).basename(".zlib").sub_ext(".#{o}.jpg").binwrite(jpg)
    end
  end

  #
  # orientation (invalid value)
  #
  data(
    "Integer#1" => {:val => 0,    :exc => RangeError},
    "Integer#2" => {:val => 9,    :exc => RangeError},
    "Float"   => {:val => 2.0,    :exc => TypeError},
    "Boolean" => {:val => true,   :exc => TypeError},
    "Array"   => {:val => [],     :exc => TypeError},
    "Symbol"  => {:val => :XXXX,  :exc => TypeError},
    "String"  => {:val => "XXXX", :exc => TypeError},
  )

  test "orientation (invalid value)" do |info|
    assert_raise_kind_of(info[:exc]) {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :YCbCr,
                        :orientation => info[:val])
    }
  end

  #
  # stride (normal)
  #
  data(
    "600" => {:val => 600, :label => "600"},
    "610" => {:val => 610, :label => "610"},
    "620" => {:val => 620, :label => "620"},
    "656" => {:val => 656, :label => "656"},
  )

  test "stride (normal)" do |info|
    enc = assert_nothing_raised {
      JPEG::Encoder.new(200, 300,
                        :pixel_format => :RGB,
                        :stride => info[:val])
    }

    org = read_raw("DSC_0215_small.rgb.zlib").bytes

    raw = "".b
    until org.empty?
      raw << org.shift(600).pack("C*x#{info[:val] - 600}")
    end

    jpg = assert_nothing_raised {enc << raw}

    if ENV['SAVE']
      lab = info[:label]
      Pathname("DSC_0215_small.#{lab}.jpg").binwrite(jpg)
    end
  end
end
