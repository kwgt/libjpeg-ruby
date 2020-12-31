require 'test/unit'
require 'base64'
require 'pathname'
require 'jpeg'

class TestDecodeOptions < Test::Unit::TestCase
  DATA_DIR  = Pathname($0).expand_path.dirname + "data"
  TEST_DATA = Base64.decode64(<<~EOD)
    /9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkS
    Ew8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJ
    CQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIy
    MjIyMjIyMjIyMjIyMjL/wAARCAEAAQADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEA
    AAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIh
    MUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6
    Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZ
    mqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx
    8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREA
    AgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAV
    YnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hp
    anN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPE
    xcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDe
    LEg0w5zTgKcqFnHpXj3PqyaAELk1m62/7hgPStF228Vl6vxbsSM8UQ+JDhucjCx3
    Ee9WMkc1VhOXbjvVk13Seo0iWFstmryHgYrPh4P1q+lZSKSJwc/jSjgimg0ucioH
    YeDxRnrTQeaMkmgdh2401jzmhjtpgk3Uw5eo8npTiTxUW7JqTdkYxQKwhJJz603J
    4pWPFNJoCwHmqlwCRireSagl6H1FXFkyRZ0q4/gPUVtMC4DCuXsyVugRXU27AoKi
    pHqiH3E2Nim4xU+ccVG3p2rBghu7tXQ6BeK0Rgbhk/UVzpBpbe5a0uUlXsefcUON
    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
    HpWTa2j3rA8he5qlorshXkz/2Q==
  EOD

  def write_ppm(img, comment = nil)
    File.open("test-#{rand(1000)}.ppm", "wb") { |f|
      f << "P6\n"
      f << "# #{comment}\n"
      f << "#{img.meta.width} #{img.meta.height}\n255\n"
      f << img
    }
  end


  #
  # pixel_format (normal)
  #

  data {
    {
      # 以下は未実装
      # "YUV422"    => {:fomrat => :YUV422,    :n_comp => 3, :times => 2},
      # "YUYV"      => {:format => :YUYV,      :n_comp => 3, :times => 2},
      # "RGB565"    => {:format => :RGB565,    :n_comp => 3, :times => 2},

      "RGB"       => {
        :in     => :RGB,
        :out    => "RGB",
        :n_comp => 3,
        :times  => 3
      },

      "RGB24"     => {
        :in     => :RGB24,
        :out    => "RGB",
        :n_comp => 3,
        :times  => 3
      },

      "BGR"       => {
        :in     => :BGR,
        :out    => "BGR",
        :n_comp => 3,
        :times  => 3
      },

      "BGR24"     => {
        :in     => :BGR24,
        :out    => "BGR",
        :n_comp => 3,
        :times  => 3
      },

      "YUV444"    => {
        :in     => :YUV444,
        :out    => "YCbCr",
        :n_comp => 3,
        :times  => 3
      },

      "YCbCr"     => {
        :in     => :YCbCr,
        :out    => "YCbCr",
        :n_comp => 3,
        :times  => 3
      },

      "RGBX"      => {
        :in     => :RGBX,
        :out    => "RGBX",
        :n_comp => 4,
        :times  => 4
      },

      "RGB32"      => {
        :in     => :RGB32,
        :out    => "RGBX",
        :n_comp => 4,
        :times  => 4
      },

      "BGRX"      => {
        :in     => :BGRX,
        :out    => "BGRX",
        :n_comp => 4,
        :times  => 4
      },

      "BGR32"      => {
        :in     => :BGR32,
        :out    => "BGRX",
        :n_comp => 4,
        :times  => 4
      },

      "GRAYSCALE" => {
        :in     => :GRAYSCALE,
        :out    => "GRAYSCALE",
        :n_comp => 1,
        :times  => 1
      },
    }
  }

  test "pixel_format (normal)" do |info|
    #
    # specify by symbol
    #
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => info[:in])
    }

    img = assert_nothing_raised {dec << TEST_DATA}
    met = img.meta

    assert_equal(256, met.width);
    assert_equal(256, met.height);
    assert_equal(256 * info[:times], met.stride);
    assert_equal(256 * 256 * info[:times], img.bytesize);
    assert_equal(info[:n_comp], met.num_components);
    assert_equal(info[:out], met.output_colorspace);

    #
    # specify by string
    #
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => info[:in].to_s)
    }

    img = assert_nothing_raised {dec << TEST_DATA}
    met = img.meta

    assert_equal(256, met.width);
    assert_equal(256, met.height);
    assert_equal(256 * info[:times], met.stride);
    assert_equal(256 * 256 * info[:times], img.bytesize);
    assert_equal(info[:n_comp], met.num_components);
    assert_equal(info[:out], met.output_colorspace);
  end

  #
  # pixel_format (not implemented value)
  #
  data {
    {
      "YUV422"    => :YUV422,
      "YUYV"      => :YUYV,
      "RGB565"    => :RGB565,
    }
  }

  test "pixel_format (not implemented value)" do |val|
    assert_raise_kind_of(NotImplementedError ) {
      JPEG::Decoder.new(:pixel_format => val)
    }
  end

  #
  # pixel_format (invalid value)
  #

  data(:integer => 1,
       :float   => 2.0,
       :boolean => true,
       :array   => [])

  test "pixel_format (invalid value)" do |val|
    #
    # specify by symbol
    #
    assert_raise_kind_of(TypeError) {
      JPEG::Decoder.new(:pixel_format => val)
    }
  end

  #
  # output_gamma (normal)
  #

  data("1 (int)" => 1,
       "0.0"     => 0.0,
       "0.5"     => 0.5,
       "2.0"     => 2.0)

  test "output_gamma (normal)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :output_gamma => val)
    }

    _img = assert_nothing_raised {dec << TEST_DATA}

=begin
    #
    # なぜか出力が変わらない？
    #
    write_ppm(_img)
=end
  end

  #
  # output_gamma (invalid value)
  #

  data("string"  => "1.0", 
       "boolean" => true,
       "array"   => [],
       "hash"    => {})

  test "output_gamma (invalid value)" do |val|
    assert_raise_kind_of(TypeError) {
      JPEG::Decoder.new(:output_gamma => val)
    }
  end

  #
  # do_fancy_upsampling
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "do_fancy_upsampling" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :do_fancy_upsampling => val)
    }

    _img = assert_nothing_raised {dec << TEST_DATA}
    # write_ppm(_img, val)
  end

  #
  # do_smoothing
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "do_smoothing" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :do_smoothing => val)
    }

    _img = assert_nothing_raised {dec << TEST_DATA}
    # write_ppm(_img, val)
  end

  #
  # dither (normal)
  #

  data("ordered"            => [:ORDERED, true,  64],
       "floyd-steinberg"    => [:FS,      false, 64],
       "8 colors"           => [:FS,      false, 8],
       "16 colors"          => [:FS,      false, 16],
       "32 colors"          => [:FS,      false, 32],
       "128 colors"         => [:FS,      false, 128],
       "256 colors"         => [:FS,      false, 256],
       "8 colors (2pass)"   => [:FS,      true,  8],
       "16 colors (2pass)"  => [:FS,      true,  16],
       "32 colors (2pass)"  => [:FS,      true,  32],
       "128 colors (2pass)" => [:FS,      true,  128],
       "256 colors (2pass)" => [:FS,      true,  256],
      )

  test "dither (normal)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :dither => val)
    }

    img = assert_nothing_raised {dec << TEST_DATA}
    # write_ppm(img, val)

    assert_equal(65536, img.bytesize);
    assert_kind_of(Array, img.meta.colormap);
    assert_true(val[2] >= img.meta.colormap.size);

    dec = assert_nothing_raised {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :dither => val,
                        :expand_colormap => true)
    }

    img = assert_nothing_raised {dec << TEST_DATA}
    # write_ppm(img, val)

    assert_equal(65536 * img.meta.num_components, img.size);
    assert_kind_of(Array, img.meta.colormap);
    assert_true(val[2] >= img.meta.colormap.size);
  end

  #
  # dither (invalid value)
  #

  data(
    "int" => {
      :val => 0,
      :exp => TypeError
    },

    "float" => {
      :val => 0.0,
      :exp => TypeError
    },

    "hash" => {
      :val => {},
      :exp => TypeError
    },

    "string" => {
      :val => "abc",
      :exp => TypeError
    },

    "true" => {
      :val => true,
      :exp => TypeError
    },

    "false" => {
      :val => false,
      :exp => TypeError
    },

    "shorty" => {
      :val => [:FS, true],
      :exp => ArgumentError
    },

    "longer" => {
      :val => [:FS, true, 64, nil],
      :exp => ArgumentError
    },

    "bad type #1" => {
      :val => [:AAAA, true, 64],
      :exp => ArgumentError
    },

    "bad type #2" => {
      :val => ["AAAA", true, 64],
      :exp => ArgumentError
    },

    "bad type #3" => {
      :val => [nil, true, 64],
      :exp => TypeError
    },

    "bad number of color #1" => {
      :val => [nil, true, nil],
      :exp => TypeError
    },

    "bad number of color #2" => {
      :val => [nil, true, "16"],
      :exp => TypeError
    },

    "range over #1" => {
      :val => [:FS, true, 2],
      :exp => RangeError
    },

    "range over #2" => {
      :val => [:FS, true, 257],
      :exp => RangeError
    },
  )

  test "dither (invalid value)" do |info|
    assert_raise_kind_of(info[:exp]) {
      JPEG::Decoder.new(:pixel_format => :RGB,
                        :dither => info[:val])
    }
  end

  #
  # without_meta
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "without_meta" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:without_meta => val)
    }

    img = assert_nothing_raised {dec << TEST_DATA}

    if val
      assert_not_respond_to(img, :meta)
    else
      assert_respond_to(img, :meta)
    end
  end

  #
  # scale (normal)
  #

  data("1"     => {:val => 1,     :size => 256},
       "2"     => {:val => 2,     :size => 512},
       "1/2"   => {:val => 1/2r,  :size => 128},
       "1/1"   => {:val => 1/1r,  :size => 256},
       "2/1"   => {:val => 2/1r,  :size => 512},
       "0.5"   => {:val => 0.5,   :size => 128},
       "1.0"   => {:val => 1.0,   :size => 256},
       "2.0"   => {:val => 2.0,   :size => 512})

  test "scale (normal)" do |info|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:scale => info[:val])
    }

    img = assert_nothing_raised {dec << TEST_DATA}
    assert_equal(info[:size], img.meta.width)
    assert_equal(info[:size], img.meta.height)
  end

  #
  # scale (bad value)
  #

  data("int(minus)" => {:val => -1,      :exp => RangeError},
       "int(0)"     => {:val => 0,       :exp => RangeError},
       "true"       => {:val => true,    :exp => TypeError},
       "false"      => {:val => false,   :exp => TypeError},
       "hash"       => {:val => {},      :exp => TypeError},
       "[]"         => {:val => [],      :exp => TypeError},
       "[1,2,3]"    => {:val => [1,2,3], :exp => TypeError},
       "string"     => {:val => "2.0",   :exp => TypeError})

  test "scale (bad value)" do |info|
    assert_raise_kind_of(info[:exp]) {
      JPEG::Decoder.new(:scale => info[:val])
    }
  end

  #
  # dct_method (normal)
  #

  data("ISLOW"   => :ISLOW,
       "IFAST"   => :IFAST,
       "FLOAT"   => :FLOAT,
       "FASTEST" => :FASTEST)

  test "dct_method (normal)" do |val|
    assert_nothing_raised {
      JPEG::Decoder.new(:dct_method => val)
    }
  end

  #
  # dct_method (bad value)
  #

  data("int"     => {:val => 1,       :exp => TypeError},
       "true"    => {:val => true,    :exp => TypeError},
       "false"   => {:val => false,   :exp => TypeError},
       "hash"    => {:val => {},      :exp => TypeError},
       "[]"      => {:val => [],      :exp => TypeError},
       "[1,2,3]" => {:val => [1,2,3], :exp => TypeError},
       "string"  => {:val => "2.0",   :exp => ArgumentError})

  test "dct_method (bad value)" do |info|
    assert_raise_kind_of(info[:exp]) {
      JPEG::Decoder.new(:dct_method => info[:val])
    }
  end

  #
  # with_exif_tags (decode)
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "with_exif_tags (decode)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:with_exif_tags => val)
    }

    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    meta  = assert_nothing_raised {(dec << dat).meta}

    if val
      assert_respond_to(meta, :exif_tags)
    else
      assert_not_respond_to(meta, :exif_tags)
    end
  end

  #
  # with_exif_tags (read header)
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "with_exif_tags (read header)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:with_exif_tags => val)
    }

    dat = (DATA_DIR + "DSC_0215_small.JPG").binread
    met = assert_nothing_raised {dec.read_header(dat)}

    if val
      assert_respond_to(met, :exif_tags)
      assert_kind_of(Hash, met.exif_tags)
    else
      assert_not_respond_to(met, :exif_tags)
    end
  end

  #
  # orientation (decode)
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "orientation (decode)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:orientation => val)
    }

    dat = (DATA_DIR + "orientation.jpg").binread
    met = assert_nothing_raised {(dec << dat).meta}

    if val
      assert_equal(met.width, 160)
      assert_equal(met.height, 240)
    else
      assert_equal(met.width, 240)
      assert_equal(met.height, 160)
    end
  end

  #
  # orientation (read header)
  #

  data("true"   => true, 
       "false"  => false,
       "int"    => 1,         # as true
       "float"  => 1.0,       # as true
       "nil"    => nil,       # as false
       "symbol" => :yes,      # as true
       "string" => "yes",     # as true
       "array"  => [],        # as true
       "hash"   => {})        # as true

  test "orientation (read header)" do |val|
    dec = assert_nothing_raised {
      JPEG::Decoder.new(:orientation => val)
    }

    dat = (DATA_DIR + "orientation.jpg").binread
    met = assert_nothing_raised {dec.read_header(dat)}

    if val
      assert_equal(met.width, 160)
      assert_equal(met.height, 240)
    else
      assert_equal(met.width, 240)
      assert_equal(met.height, 160)
    end
  end
end

