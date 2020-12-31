require 'test/unit'
require 'base64'
require 'pathname'
require 'jpeg'

class TestReadHeader < Test::Unit::TestCase
  DATA_DIR = Pathname($0).expand_path.dirname + "data"

  #
  # read header (normal)
  #

  test "read header (normal)" do
    dec = JPEG::Decoder.new
    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    met = assert_nothing_raised {dec.read_header(dat)}

    assert_respond_to(met, :width)
    assert_respond_to(met, :stride)
    assert_respond_to(met, :height)
    assert_respond_to(met, :original_colorspace)
    assert_respond_to(met, :output_colorspace)
    assert_respond_to(met, :num_components)
    assert_not_respond_to(met, :exif_tags)
    assert_not_respond_to(met, :exif)

    assert_kind_of(Integer, met.width)
    assert_kind_of(Integer, met.stride)
    assert_kind_of(Integer, met.height)
    assert_kind_of(String, met.original_colorspace)
    assert_kind_of(String, met.output_colorspace)
    assert_kind_of(Integer, met.num_components)
  end

  #
  # read header (with Exif tags)
  #

  data("currently" => :with_exif_tags)

  test "read header (with Exif tags)" do |opt|
    dec = JPEG::Decoder.new(opt => true)
    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    met = assert_nothing_raised {dec.read_header(dat)}

    assert_respond_to(met, :width)
    assert_respond_to(met, :stride)
    assert_respond_to(met, :height)
    assert_respond_to(met, :original_colorspace)
    assert_respond_to(met, :output_colorspace)
    assert_respond_to(met, :num_components)
    assert_respond_to(met, :exif_tags)
    assert_respond_to(met, :exif)

    assert_kind_of(Integer, met.width)
    assert_kind_of(Integer, met.stride)
    assert_kind_of(Integer, met.height)
    assert_kind_of(String, met.original_colorspace)
    assert_kind_of(String, met.output_colorspace)
    assert_kind_of(Integer, met.num_components)
    assert_kind_of(Hash, met.exif_tags)
    assert_kind_of(Hash, met.exif)

    assert_equal("NIKON CORPORATION", met.exif_tags[:maker])
    assert_equal("NIKON D5200", met.exif_tags[:model])
    assert_equal(1, met.exif_tags[:orientation])
    assert_equal(met.width, met.exif_tags.dig(:exif, :pixel_x_dimension))
    assert_equal(met.height, met.exif_tags.dig(:exif, :pixel_y_dimension))

    assert_equal(met.exif_tags, met.exif)
  end

  #
  # on decode (normal)
  #

  test "on decode (normal)" do
    dec = JPEG::Decoder.new
    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    met = assert_nothing_raised {(dec << dat).meta}

    assert_respond_to(met, :width)
    assert_respond_to(met, :stride)
    assert_respond_to(met, :height)
    assert_respond_to(met, :original_colorspace)
    assert_respond_to(met, :output_colorspace)
    assert_respond_to(met, :num_components)
    assert_not_respond_to(met, :exif_tags)
    assert_not_respond_to(met, :exif)

    assert_kind_of(Integer, met.width)
    assert_kind_of(Integer, met.stride)
    assert_kind_of(Integer, met.height)
    assert_kind_of(String, met.original_colorspace)
    assert_kind_of(String, met.output_colorspace)
    assert_kind_of(Integer, met.num_components)
  end

  #
  # on decode (with Exif tags)
  #

  data("currently" => :with_exif_tags)

  test "on decode (with Exif tags)" do |opt|
    dec = JPEG::Decoder.new(opt => true)
    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    met = assert_nothing_raised {(dec << dat).meta}

    assert_respond_to(met, :width)
    assert_respond_to(met, :stride)
    assert_respond_to(met, :height)
    assert_respond_to(met, :original_colorspace)
    assert_respond_to(met, :output_colorspace)
    assert_respond_to(met, :num_components)
    assert_respond_to(met, :exif_tags)
    assert_respond_to(met, :exif)

    assert_kind_of(Integer, met.width)
    assert_kind_of(Integer, met.stride)
    assert_kind_of(Integer, met.height)
    assert_kind_of(String, met.original_colorspace)
    assert_kind_of(String, met.output_colorspace)
    assert_kind_of(Integer, met.num_components)
    assert_kind_of(Hash, met.exif_tags)
    assert_kind_of(Hash, met.exif)

    assert_equal("NIKON CORPORATION", met.exif_tags[:maker])
    assert_equal("NIKON D5200", met.exif_tags[:model])
    assert_equal(1, met.exif_tags[:orientation])
    assert_equal(met.width, met.exif_tags.dig(:exif, :pixel_x_dimension))
    assert_equal(met.height, met.exif_tags.dig(:exif, :pixel_y_dimension))

    assert_equal(met.exif_tags, met.exif)

    # thumbnail
    thumb = met.exif_tags[:thumbnail]
    assert_kind_of(Hash, thumb)
    assert_kind_of(String, thumb[:jpeg_interchange])
    assert_equal("ASCII-8BIT", thumb[:jpeg_interchange].encoding.to_s)

    assert_nothing_raised {dec << thumb[:jpeg_interchange]}
  end

  #
  # without metadata decode
  #

  test "without metadata decode" do
    dec = JPEG::Decoder.new(:without_meta => true)
    dat = (DATA_DIR + "DSC_0215_small.JPG").binread

    img = assert_nothing_raised {dec << dat}

    assert_not_respond_to(img, :meta)
  end
end
