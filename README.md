# libjpeg-ruby
libjpeg interface for ruby.

## Installation

```ruby
gem 'libjpeg-ruby'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install libjpeg-ruby

If you need to specify the libjpeg path, use the following options:

    $ gem install libjpeg-ruby -- --with-jpeg-include ~/env/opts/include --with-jpeg-lib ~/env/opts/lib
## Usage

### decode sample

```ruby
require 'jpeg'

dec = JPEG::Decoder.new(:pixel_format => :BGR)

p dec.read_header(IO.binread("test.jpg")

raw = dec << IO.binread("test.jpg")
p raw.meta

IO.binwrite("test.bgr", raw)
```

#### decode options
| option | value type | description |
|---|---|---|
| :pixel_format | String or Symbol | output format |
| :output_gamma | Float | gamma value |
| :fancy_upsampling | Boolean | T.B.D |
| :do_smoothing | Boolean | T.B.D |
| :opt_dither | [{str}MODE, {bool}2PASS, {int}NUM_COLORS] | T.B.D |
| :use_1ass_quantizer | Boolean | T.B.D |
| :use_external_colormap | Boolean | T.B.D |
| :use_2pass_quantizer | Boolean | T.B.D |
| :without_meta | Boolean | T.B.D |
| :expand_colormap | Booblean | T.B.D |
| :scale | Rational or Float | T.B.D |
| :dct_method | String or Symbol | T.B.D |
| :with_exif_tags | Boolean | Specify whether to read Exif tag. When set to true, the content of Exif tag will included in the meta information. |
| :orientation | Boolean | Specify whether to parse Exif orientation. When set to true, apply orientation for decode result. |

#### supported output format
RGB RGB24 YUV422 YUYV RGB565 YUV444 YCbCr BGR BGR24 RGBX RGB32 BGRX BGR32 

#### supported DCT method
ISLOW IFAST FLOAT FASTEST

### encode sample

```ruby
require 'jpeg'

enc = JPEG::Encoder.new(640, 480, :pixel_format => :YCbCr)

IO.binwrite("test.jpg", enc << IO.binread("test.raw"))
```
#### encode option
#### encode options
| option | value type | description |
|---|---|---|
| :pixel_fromat | String or Symbol | input format |
| :quality | Integer | encode quality (0-100) |
| :scale | Rational or Float | |
| :dct_method | String or Symbol | T.B.D |
| :orientation | Integer | Specify Exif orientation value (1-8). |

