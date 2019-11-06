/*
 * JPEG encode/decode library for Ruby
 *
 *  Copyright (C) 2015 Hiroshi Kuwagata <kgt9221@gmail.com>
 */

#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <setjmp.h>

#include <jpeglib.h>

#include "ruby.h"
#include "ruby/encoding.h"

#define UNIT_LINES                 10

#ifdef DEFAULT_QUALITY
#undef DEFAULT_QUALITY
#endif /* defined(DEFAULT_QUALITY) */

#define DEFAULT_QUALITY            75
#define DEFAULT_INPUT_COLOR_SPACE  JCS_YCbCr
#define DEFAULT_INPUT_COMPONENTS   2
#define DEFAULT_FLAGS              (F_NEED_META)

#define F_NEED_META                0x00000001
#define F_EXPAND_COLORMAP          0x00000002
#define F_PARSE_EXIF               0x00000004
#define F_APPLY_ORIENTATION        0x00000008
#define F_DITHER                   0x00000010

#define SET_FLAG(ptr, msk)         ((ptr)->flags |= (msk))
#define CLR_FLAG(ptr, msk)         ((ptr)->flags &= ~(msk))
#define TEST_FLAG(ptr, msk)        ((ptr)->flags & (msk))
#define TEST_FLAG_ALL(ptr, msk)    (((ptr)->flags & (msk)) == (msk))

#define FMT_YUV422                 1
#define FMT_RGB565                 2
#define FMT_GRAYSCALE              3
#define FMT_YUV                    4
#define FMT_RGB                    5
#define FMT_BGR                    6
#define FMT_RGB32                  7
#define FMT_BGR32                  8

#define FMT_YVU                    20     /* original extend */

#define JPEG_APP1                  0xe1   /* Exif marker */

#define N(x)                       (sizeof(x)/sizeof(*x))
#define SWAP(a,b,t) \
        do {t c; c = (a); (a) = (b); (b) = c;} while (0)

#define RUNTIME_ERROR(msg)         rb_raise(rb_eRuntimeError, (msg))
#define ARGUMENT_ERROR(msg)        rb_raise(rb_eArgError, (msg))
#define TYPE_ERROR(msg)            rb_raise(rb_eTypeError, (msg))
#define RANGE_ERROR(msg)           rb_raise(rb_eRangeError, (msg))
#define NOT_IMPLEMENTED_ERROR(msg) rb_raise(rb_eNotImpError, (msg))

#define IS_COLORMAPPED(ci)         (((ci)->actual_number_of_colors > 0) &&\
                                    ((ci)->colormap != NULL) && \
                                    ((ci)->output_components == 1) && \
                                       (((ci)->out_color_components == 1) || \
                                        ((ci)->out_color_components == 3)))

#define ALLOC_ARRAY() \
        ((JSAMPARRAY)xmalloc(sizeof(JSAMPROW) * UNIT_LINES))
#define ALLOC_ROWS(w,c) \
        ((JSAMPROW)xmalloc(sizeof(JSAMPLE) * (w) * (c) * UNIT_LINES))

#define EQ_STR(val,str)            (rb_to_id(val) == rb_intern(str))
#define EQ_INT(val,n)              (FIX2INT(val) == n)

static VALUE module;
static VALUE encoder_klass;
static VALUE encerr_klass;

static VALUE decoder_klass;
static VALUE meta_klass;
static VALUE decerr_klass;

static ID id_meta;
static ID id_width;
static ID id_stride;
static ID id_height;
static ID id_orig_cs;
static ID id_out_cs;
static ID id_ncompo;
static ID id_exif_tags;
static ID id_colormap;

typedef struct {
  int tag;
  const char* name;
} tag_entry_t;

tag_entry_t tag_tiff[] = {
  /* 0th IFD */
  {0x0100, "image_width",                  },
  {0x0101, "image_length",                 },
  {0x0102, "bits_per_sample",              },
  {0x0103, "compression",                  },
  {0x0106, "photometric_interpretation",   },
  {0x010e, "image_description",            },
  {0x010f, "maker",                        },
  {0x0110, "model",                        },
  {0x0111, "strip_offsets",                },
  {0x0112, "orientation",                  },
  {0x0115, "sample_per_pixel",             },
  {0x0116, "rows_per_strip",               },
  {0x0117, "strip_byte_counts",            },
  {0x011a, "x_resolution",                 },
  {0x011b, "y_resolution",                 },
  {0x011c, "planer_configuration",         },
  {0x0128, "resolution_unit",              },
  {0x012d, "transfer_function",            },
  {0x0131, "software",                     },
  {0x0132, "date_time",                    },
  {0x013b, "artist",                       },
  {0x013e, "white_point",                  },
  {0x013f, "primary_chromaticities",       },
  {0x0201, "jpeg_interchange_format",      },
  {0x0202, "jpeg_interchange_format_length"},
  {0x0211, "ycbcr_coefficients",           },
  {0x0212, "ycbcr_sub_sampling",           },
  {0x0213, "ycbcr_positioning",            },
  {0x0214, "reference_black_white",        },
  {0x0d68, "copyright",                    },
  {0x8298, "copyright",                    },
  {0x8769, NULL,                           }, /* ExifIFDPointer    */
  {0x8825, NULL,                           }, /* GPSInfoIFDPointer */
  {0xc4a5, "print_im",                     },
};

tag_entry_t tag_exif[] = {
  /* Exif IFD */
  {0x829a, "exposure_time",                },
  {0x829d, "f_number",                     },
  {0x8822, "exposure_program",             },
  {0x8824, "spectral_sensitivity",         },
  {0x8827, "iso_speed_ratings",            },
  {0x8828, "oecf",                         },
  {0x882a, "time_zone_offset",             },
  {0x882b, "self_timer_mode",              },
  {0x8830, "sensitivity_type",             },
  {0x8831, "standard_output_sensitivity",  },
  {0x8832, "recommended_exposure_index",   },
  {0x9000, "exif_version",                 },
  {0x9003, "data_time_original",           },
  {0x9004, "data_time_digitized",          },
  {0x9010, "offset_time",                  },
  {0x9011, "offset_time_original",         },
  {0x9012, "offset_time_digitized",        },
  {0x9101, "color_space",                  },
  {0x9102, "components_configuration",     },
  {0x9201, "shutter_speed_value",          },
  {0x9202, "apertutre_value",              },
  {0x9203, "brightness_value",             },
  {0x9204, "exposure_bias_value",          },
  {0x9205, "max_aperture_value",           },
  {0x9206, "subject_distance",             },
  {0x9207, "metering_mode",                },
  {0x9208, "light_source",                 },
  {0x9209, "flash",                        },
  {0x920a, "focal_length",                 },
  {0x927c, "marker_note",                  },
  {0x9286, "user_comment",                 },
  {0x9290, "sub_sec_time",                 },
  {0x9291, "sub_sec_time_original",        },
  {0x9292, "sub_sec_time_digitized",       },
  {0xa000, "flash_pix_version",            },
  {0xa001, "color_space",                  },
  {0xa002, "pixel_x_dimension",            },
  {0xa003, "pixel_y_dimension",            },
  {0xa004, "related_sound_file",           },
  {0xa005, NULL,                           }, /* InteroperabilityIFDPointer */
  {0xa20b, "flash_energy",                 },
  {0xa20b, "flash_energy",                 },
  {0xa20c, "spatial_frequency_response",   },
  {0xa20e, "focal_panel_x_resolution",     },
  {0xa20f, "focal_panel_y_resolution",     },
  {0xa210, "focal_panel_resolution_unit",  },
  {0xa214, "subject_location",             },
  {0xa215, "exposure_index",               },
  {0xa217, "sensing_method",               },
  {0xa300, "file_source",                  },
  {0xa301, "scene_type",                   },
  {0xa302, "cfa_pattern",                  },
  {0xa401, "custom_rendered",              },
  {0xa402, "exposure_mode",                },
  {0xa403, "white_balance",                },
  {0xa404, "digital_zoom_ratio",           },
  {0xa405, "focal_length_in_35mm_film",    },
  {0xa406, "scene_capture_type",           },
  {0xa407, "gain_control",                 },
  {0xa408, "contrast",                     },
  {0xa409, "sturation",                    },
  {0xa40a, "sharpness",                    },
  {0xa40b, "device_setting_description",   },
  {0xa40c, "subject_distance_range",       },
  {0xa420, "image_unique_id",              },
  {0xa430, "owner_name",                   },
  {0xa431, "serial_number",                },
  {0xa432, "lens_info",                    },
  {0xa433, "lens_make",                    },
  {0xa434, "lens_model",                   },
  {0xa435, "lens_serial_number",           },
};

tag_entry_t tag_gps[] = {
  /* GPS IFD */
  {0x0000, "version_id",                   },
  {0x0001, "latitude_ref",                 },
  {0x0002, "latitude",                     },
  {0x0003, "longitude_ref",                },
  {0x0004, "longitude",                    },
  {0x0005, "altitude_ref",                 },
  {0x0006, "altitude",                     },
  {0x0007, "timestamp",                    },
  {0x0008, "satellites",                   },
  {0x0009, "status",                       },
  {0x000a, "measure_mode",                 },
  {0x000b, "dop",                          },
  {0x000c, "speed_ref",                    },
  {0x000d, "speed",                        },
  {0x000e, "track_ref",                    },
  {0x000f, "track",                        },
  {0x0010, "img_direction_ref",            },
  {0x0011, "img_direction",                },
  {0x0012, "map_datum",                    },
  {0x0013, "dest_latitude_ref",            },
  {0x0014, "dest_latitude",                },
  {0x0015, "dest_longitude_ref",           },
  {0x0016, "dest_longitude",               },
  {0x0017, "bearing_ref",                  },
  {0x0018, "bearing",                      },
  {0x0019, "dest_distance_ref",            },
  {0x001a, "dest_distance",                },
  {0x001b, "processing_method",            },
  {0x001c, "area_infotmation",             },
  {0x001d, "date_stamp",                   },
  {0x001e, "differential",                 },
};

tag_entry_t tag_i14y[] = {
  /* Interoperability IFD */
  {0x0001, "interoperability_index",       },
  {0x0002, "interoperability_version",     },
  {0x1000, "related_image_file_format",    },
  {0x1001, "related_image_width",          },
};

static const char* encoder_opts_keys[] = {
  "pixel_format",             // {str}
  "quality",                  // {integer}
  "scale",                    // {rational} or {float}
  "dct_method",               // {str}
  "orientation"               // {integer}
  "stride"                    // {integer}
};

static ID encoder_opts_ids[N(encoder_opts_keys)];

typedef struct {
  int format;
  int width;
  int stride;
  int height;

  int data_size;
  J_DCT_METHOD dct_method;

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  JSAMPARRAY array;
  JSAMPROW rows;

  int orientation;
} jpeg_encode_t;

static const char* decoder_opts_keys[] = {
  "pixel_format",             // {str}
  "output_gamma",             // {float}
  "do_fancy_upsampling",      // {bool}
  "do_smoothing",             // {bool}
  "dither",                   // [{str}MODE, {bool}2PASS, {int}NUM_COLORS]
#if 0
  "use_1pass_quantizer",      // {bool}
  "use_external_colormap",    // {bool}
  "use_2pass_quantizer",      // {bool}
#endif
  "without_meta",             // {bool}
  "expand_colormap",          // {bool}
  "scale",                    // {rational} or {float}
  "dct_method",               // {str}
  "with_exif",                // {bool}
  "with_exif_tags",           // {bool}
  "orientation"               // {bool}
};

static ID decoder_opts_ids[N(decoder_opts_keys)];

typedef struct {
  struct jpeg_error_mgr jerr;

  char msg[JMSG_LENGTH_MAX+10];
  jmp_buf jmpbuf;
} ext_error_t;

typedef struct {
  int flags;
  int format;

  J_COLOR_SPACE out_color_space;
  int scale_num;
  int scale_denom;
  int out_color_components;
  double output_gamma;
  boolean buffered_image;
  boolean do_fancy_upsampling;
  boolean do_block_smoothing;
  boolean quantize_colors;
  J_DITHER_MODE dither_mode;
  J_DCT_METHOD dct_method;
  boolean two_pass_quantize;
  int desired_number_of_colors;
  boolean enable_1pass_quant;
  boolean enable_external_quant;
  boolean enable_2pass_quant;

  struct jpeg_decompress_struct cinfo;
  ext_error_t err_mgr;

  struct {
    int value;
    VALUE buf;
  } orientation;
} jpeg_decode_t;


static VALUE
lookup_tag_symbol(tag_entry_t* tbl, size_t n, int tag)
{
  VALUE ret;
  int l;
  int r;
  int i;
  tag_entry_t* p;
  char buf[16];

  ret = Qundef;
  l   = 0;
  r   = n - 1;

  while (r >= l) {
    i = (l + r) / 2;
    p = tbl + i;

    if (p->tag < tag) {
      l = i + 1;
      continue;
    }

    if (p->tag > tag) {
      r = i - 1;
      continue;
    }

    ret = (p->name)? ID2SYM(rb_intern(p->name)): Qnil;
    break;
  }

  if (ret == Qundef) {
    sprintf(buf, "tag_%04x", tag);
    ret = ID2SYM(rb_intern(buf));
  }

  return ret;
}

static void
encode_output_message(j_common_ptr cinfo)
{
  char msg[JMSG_LENGTH_MAX];

  (*cinfo->err->format_message)(cinfo, msg);
}

static void
encode_error_exit(j_common_ptr cinfo)
{
  char msg[JMSG_LENGTH_MAX];

  (*cinfo->err->format_message)(cinfo, msg);

  jpeg_destroy_compress((j_compress_ptr)cinfo);
  rb_raise(encerr_klass, "%s", msg);
}


static void
rb_encoder_free( void* _ptr)
{
  jpeg_encode_t* ptr;

  ptr = (jpeg_encode_t*)_ptr;

  if (ptr->array != NULL) xfree(ptr->array);
  if (ptr->rows != NULL) xfree(ptr->rows);

  jpeg_destroy_compress(&ptr->cinfo);

  free(_ptr);
}

static VALUE
rb_encoder_alloc(VALUE self)
{
  jpeg_encode_t* ptr;

  ptr = ALLOC(jpeg_encode_t);
  memset(ptr, 0, sizeof(*ptr));

  return Data_Wrap_Struct(encoder_klass, 0, rb_encoder_free, ptr);
}

static void
set_encoder_context(jpeg_encode_t* ptr, int wd, int ht, VALUE opt)
{
  VALUE opts[N(encoder_opts_ids)];
  int format;
  int color_space;
  int components;
  int data_size;
  int quality;
  int scale_num;
  int scale_denom;
  int stride;
  int i;

  /*
   * parse options
   */
  rb_get_kwargs(opt, encoder_opts_ids, 0, N(encoder_opts_ids), opts);

  /*
   * eval :pixel_format option
   */
  if (opts[0] == Qundef||EQ_STR(opts[0], "YUV422")||EQ_STR(opts[0], "YUYV")) {
    format      = FMT_YUV422;
    color_space = JCS_YCbCr;
    components  = 3;
    data_size   = (wd * ht * 2);

  } else if (EQ_STR(opts[0], "RGB565")) {
    format      = FMT_RGB565;
    color_space = JCS_RGB;
    components  = 3;
    data_size   = (wd * ht * 2);

  } else if (EQ_STR(opts[0], "RGB") || EQ_STR(opts[0], "RGB24")) {
    format      = FMT_RGB;
    color_space = JCS_RGB;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "BGR") || EQ_STR(opts[0], "BGR24")) {
    format      = FMT_BGR;
    color_space = JCS_EXT_BGR;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "YUV444") || EQ_STR(opts[0], "YCbCr")) {
    format      = FMT_YUV;
    color_space = JCS_YCbCr;
    components  = 3;
    data_size   = (wd * ht * 3);

  } else if (EQ_STR(opts[0], "RGBX") || EQ_STR(opts[0], "RGB32")) {
    format      = FMT_RGB32;
    color_space = JCS_EXT_RGBX;
    components  = 4;
    data_size   = (wd * ht * 4);


  } else if (EQ_STR(opts[0], "BGRX") || EQ_STR(opts[0], "BGR32")) {
    format      = FMT_BGR32;
    color_space = JCS_EXT_BGRX;
    components  = 4;
    data_size   = (wd * ht * 4);


  } else if (EQ_STR(opts[0], "GRAYSCALE")) {
    format      = FMT_GRAYSCALE;
    color_space = JCS_GRAYSCALE;
    components  = 1;
    data_size   = (wd * ht);

  } else {
    ARGUMENT_ERROR("Unsupportd :pixel_format option value.");
  }

  /*
   * eval :quality option
   */
  if (opts[1] == Qundef) {
    quality = DEFAULT_QUALITY;

  } else {
    if (TYPE(opts[1]) != T_FIXNUM) {
      ARGUMENT_ERROR("Unsupportd :quality option value.");

    } else {
      quality = FIX2INT(opts[1]);
    }

    if (quality < 0) {
      ARGUMENT_ERROR(":quality value is to little.");

    } else if (quality > 100) {
      ARGUMENT_ERROR(":quality value is to big.");
    }
  }
  
  /*
   * eval scale option
   */
  (void)scale_num;
  (void)scale_denom;

  switch (TYPE(opts[2])) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FLOAT:
    scale_num   = (int)(NUM2DBL(opts[2]) * 1000.0);
    scale_denom = 1000;
    break;

  case T_RATIONAL:
    scale_num   = FIX2INT(rb_rational_num(opts[2]));
    scale_denom = FIX2INT(rb_rational_den(opts[2]));
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :scale option value.");
    break;
  }

  /*
   * eval dct_method option
   */
  if (opts[3] == Qundef || EQ_STR(opts[3], "FASTEST")) {
    ptr->dct_method = JDCT_FASTEST;

  } else if (EQ_STR(opts[3], "ISLOW")) {
    ptr->dct_method = JDCT_ISLOW;

  } else if (EQ_STR(opts[3], "IFAST")) {
    ptr->dct_method = JDCT_IFAST;

  } else if (EQ_STR(opts[3], "FLOAT")) {
    ptr->dct_method = JDCT_FLOAT;

  } else {
    ARGUMENT_ERROR("Unsupportd :dct_method option value.");
  }

  /*
   * eval orientation option
   */
  switch (TYPE(opts[4])) {
  case T_UNDEF:
    ptr->orientation = 0;
    break;

  case T_FIXNUM:
    ptr->orientation = FIX2INT(opts[4]);
    if (ptr->orientation < 1 || ptr->orientation > 8) {
      RANGE_ERROR("orientation is ouf range");
    } 
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :orientation option value.");
  }

  /*
   * eval stride option
   */
  switch (TYPE(opts[5])) {
  case T_UNDEF:
    stride = wd * components;
    break;

  case T_FIXNUM:
    stride = FIX2INT(opts[5]);
    break;

  default:
    ARGUMENT_ERROR("Unsupportd :stride option value.");
  }


  /*
   * set context
   */
  ptr->format    = format;
  ptr->width     = wd;
  ptr->stride    = stride;
  ptr->height    = ht;
  ptr->data_size = data_size;
  ptr->array     = ALLOC_ARRAY();
  ptr->rows      = ALLOC_ROWS(wd, components);

  for (i = 0; i < UNIT_LINES; i++) {
    ptr->array[i] = ptr->rows + (i * components * wd);
  }

  jpeg_create_compress(&ptr->cinfo);

  ptr->cinfo.err              = jpeg_std_error(&ptr->jerr);
  ptr->jerr.output_message    = encode_output_message;
  ptr->jerr.error_exit        = encode_error_exit;

  ptr->cinfo.image_width      = wd;
  ptr->cinfo.image_height     = ht;
  ptr->cinfo.in_color_space   = color_space;
  ptr->cinfo.input_components = components;

  ptr->cinfo.optimize_coding  = TRUE;
  ptr->cinfo.arith_code       = TRUE;
  ptr->cinfo.raw_data_in      = FALSE;
  ptr->cinfo.dct_method       = ptr->dct_method;

  jpeg_set_defaults(&ptr->cinfo);
  jpeg_set_quality(&ptr->cinfo, quality, TRUE);
  jpeg_suppress_tables(&ptr->cinfo, TRUE);
}

/**
 * initialize encoder object
 *
 * @overload initialize(width, height, opts)
 *
 *   @param width [Integer] width of input image (px)
 *   @param height [Integer] height of input image (px)
 *   @param opts [Hash] options to initialize object
 *
 *   @option opts [Symbol] :pixel_format
 *     specifies the format of the input image. possible values are:
 *     YUV422 YUYV RGB565 RGB RGB24 BGR BGR24 YUV444 YCbCr
 *     RGBX RGB32 BGRX BGR32 GRAYSCALE
 *
 *   @option opts [Integer] :quality
 *     specifies the quality of the compressed image.
 *     You can specify from 0 (lowest) to 100 (best).
 *
 *   @option opts [Symbol] :dct_method
 *     specifies how encoding is handled. possible values are:
 *     FASTEST ISLOW IFAST FLOAT
 */
static VALUE
rb_encoder_initialize(int argc, VALUE *argv, VALUE self)
{
  jpeg_encode_t* ptr;
  VALUE wd;
  VALUE ht;
  VALUE opt;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_encode_t, ptr);

  /*
   * parse arguments
   */
  rb_scan_args(argc, argv, "21", &wd, &ht, &opt);

  Check_Type(wd, T_FIXNUM);
  Check_Type(ht, T_FIXNUM);
  if (opt != Qnil) Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  set_encoder_context(ptr, FIX2INT(wd), FIX2INT(ht), opt);

  return Qtrue;
}

static void
push_rows_yuv422(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * nrow;

  for (i = 0; i < size; i += 2) {
    rows[0] = data[0];
    rows[1] = data[1];
    rows[2] = data[3];
    rows[3] = data[2];
    rows[4] = data[1];
    rows[5] = data[3];

    rows += 6;
    data += 4;
  }
}

static void
push_rows_rgb565(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * nrow;

  for (i = 0; i < size; i += 1) {
    rows[0] = data[1] & 0xf8;
    rows[1] = ((data[1] << 5) & 0xe0) | ((data[0] >> 3) & 0x1c);
    rows[2] = (data[0] << 3) & 0xf8;

    rows += 3;
    data += 2;
  }
}

static void
push_rows_comp3(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow * 3;
  memcpy(rows, data, size);
}

static void
push_rows_comp4(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow * 4;
  memcpy(rows, data, size);
}



static void
push_rows_grayscale(JSAMPROW rows, int wd, uint8_t* data, int nrow)
{
  int size;

  size = wd * nrow;
  memcpy(rows, data, size);
}

static void
push_rows(jpeg_encode_t* ptr, uint8_t* data, int nrow)
{
  int ret;

  switch (ptr->format) {
  case FMT_YUV422:
    push_rows_yuv422(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_RGB565:
    push_rows_rgb565(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_YUV:
  case FMT_RGB:
  case FMT_BGR:
    push_rows_comp3(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_RGB32:
  case FMT_BGR32:
    push_rows_comp4(ptr->rows, ptr->width, data, nrow);
    break;

  case FMT_GRAYSCALE:
    push_rows_grayscale(ptr->rows, ptr->width, data, nrow);
    break;

  default:
    RUNTIME_ERROR("Really?");
  }
}

static void
put_exif_tags(jpeg_encode_t* ptr)
{
  uint8_t data[] = {
    /* Exif header */
    'E',  'x',  'i',  'f', 0x00, 0x00,
    'M',  'M',
    0x00, 0x2a,
    0x00, 0x00, 0x00, 0x08,
    0x00, 0x01,

    /* orieatation */
    0x01, 0x12,
    0x00, 0x03,
    0x00, 0x00, 0x00, 0x01,
    0x00, 0x00,
    0x00, 0x00,

    /* end mark */
    0x00, 0x00, 0x00, 0x00,
  };

  data[24] = (ptr->orientation >> 8) & 0xff;
  data[25] = (ptr->orientation >> 0) & 0xff;

  jpeg_write_marker(&ptr->cinfo, JPEG_APP1, data, sizeof(data));
}

static VALUE
do_encode(jpeg_encode_t* ptr, uint8_t* data)
{
  VALUE ret;

  unsigned char* buf;
  unsigned long buf_size;
  int nrow;

  buf = NULL;

  jpeg_mem_dest(&ptr->cinfo, &buf, &buf_size); 

  jpeg_start_compress(&ptr->cinfo, TRUE);

  if (ptr->orientation != 0) {
    put_exif_tags(ptr);
  }

  while (ptr->cinfo.next_scanline < ptr->cinfo.image_height) {
    nrow = ptr->cinfo.image_height - ptr->cinfo.next_scanline;
    if (nrow > UNIT_LINES) nrow = UNIT_LINES;

    push_rows(ptr, data, nrow);

    jpeg_write_scanlines(&ptr->cinfo, ptr->array, nrow);
    data += ptr->stride;
  }

  jpeg_finish_compress(&ptr->cinfo);

  /*
   * create return data
   */
  ret = rb_str_buf_new(buf_size);
  rb_str_set_len(ret, buf_size);

  memcpy(RSTRING_PTR(ret), buf, buf_size);

  /*
   * post process
   */
  free(buf);

  return ret;
}

/**
 * encode data
 *
 * @overload encode(raw)
 *
 *   @param raw [String]  raw image data to encode.
 *
 *   @return [String] encoded JPEG data.
 */
static VALUE
rb_encoder_encode(VALUE self, VALUE data)
{
  VALUE ret;
  jpeg_encode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_encode_t, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  if (RSTRING_LEN(data) < ptr->data_size) {
    ARGUMENT_ERROR("raw image data is too short.");
  }

  if (RSTRING_LEN(data) > ptr->data_size) {
    ARGUMENT_ERROR("raw image data is too large.");
  }

  /*
   * do encode
   */
  ret = do_encode(ptr, (uint8_t*)RSTRING_PTR(data));

  return ret;
}

static void
rb_decoder_free(void* ptr)
{
  //jpeg_destroy_decompress(&(((jpeg_decode_t*)ptr)->cinfo));
  free(ptr);
}

static void
rb_decoder_mark(void* _ptr)
{
  jpeg_decode_t* ptr;

  ptr = (jpeg_decode_t*)_ptr; 

  if (ptr->orientation.buf != Qnil) {
    rb_gc_mark(ptr->orientation.buf);
  }
}

static void
decode_output_message(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;

  (*err->jerr.format_message)(cinfo, err->msg);
}

static void
decode_emit_message(j_common_ptr cinfo, int msg_level)
{
  ext_error_t* err;

  if (msg_level < 0) {
    err = (ext_error_t*)cinfo->err;
    (*err->jerr.format_message)(cinfo, err->msg);
    /*
     * 以前はemit_messageが呼ばれるとエラー扱いしていたが、
     * Logicoolの一部のモデルなどで問題が出るので無視する
     * ようにした。
     * また本来であれば、警告表示を行うべきでもあるが一部
     * のモデルで大量にemitされる場合があるので表示しない
     * ようにしている。
     * 問題が発生した際は、最後のメッセージをオブジェクト
     * のインスタンスとして持たすべき。
     */
    // longjmp(err->jmpbuf, 1);
  }
}

static void
decode_error_exit(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;
  (*err->jerr.format_message)(cinfo, err->msg);
  longjmp(err->jmpbuf, 1);
}

static VALUE
rb_decoder_alloc(VALUE self)
{
  jpeg_decode_t* ptr;

  ptr = ALLOC(jpeg_decode_t);
  memset(ptr, 0, sizeof(*ptr));

  ptr->flags                    = DEFAULT_FLAGS;
  ptr->format                   = FMT_RGB;

  ptr->out_color_space          = JCS_RGB;

  ptr->scale_num                = 1;
  ptr->scale_denom              = 1;

  ptr->out_color_components     = 3;
  ptr->output_gamma             = 0.0;
  ptr->buffered_image           = FALSE;
  ptr->do_fancy_upsampling      = FALSE;
  ptr->do_block_smoothing       = FALSE;
  ptr->quantize_colors          = FALSE;
  ptr->dither_mode              = JDITHER_NONE;
  ptr->dct_method               = JDCT_FASTEST;
  ptr->desired_number_of_colors = 0;
  ptr->enable_1pass_quant       = FALSE;
  ptr->enable_external_quant    = FALSE;
  ptr->enable_2pass_quant       = FALSE;

  ptr->orientation.value        = 0;
  ptr->orientation.buf          = Qnil;

  return Data_Wrap_Struct(decoder_klass, rb_decoder_mark, rb_decoder_free, ptr);
}

static void
eval_decoder_opt_pixel_format(jpeg_decode_t* ptr, VALUE opt)
{
  int format;
  int color_space;
  int components;

  if (opt != Qundef) {
    if(EQ_STR(opt, "RGB") || EQ_STR(opt, "RGB24")) {
      format      = FMT_RGB;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "YUV422") || EQ_STR(opt, "YUYV")) {
      format      = FMT_YUV422;
      color_space = JCS_YCbCr;
      components  = 3;

      NOT_IMPLEMENTED_ERROR( "not implemented colorspace");

    } else if (EQ_STR(opt, "RGB565")) {
      format      = FMT_RGB565;
      color_space = JCS_RGB;
      components  = 3;

      NOT_IMPLEMENTED_ERROR( "not implemented colorspace");

    } else if (EQ_STR(opt, "GRAYSCALE")) {
      format      = FMT_GRAYSCALE;
      color_space = JCS_GRAYSCALE;
      components  = 1;

    } else if (EQ_STR(opt, "YUV444") || EQ_STR(opt, "YCbCr")) {
      format      = FMT_YUV;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "BGR") || EQ_STR(opt, "BGR24")) {
      format      = FMT_BGR;
      color_space = JCS_EXT_BGR;
      components  = 3;

    } else if (EQ_STR(opt, "YVU444") || EQ_STR(opt, "YCrCb")) {
      format      = FMT_YVU;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "RGBX") || EQ_STR(opt, "RGB32")) {
      format      = FMT_RGB32;
      color_space = JCS_EXT_RGBX;
      components  = 4;

    } else if (EQ_STR(opt, "BGRX") || EQ_STR(opt, "BGR32")) {
      format      = FMT_BGR32;
      color_space = JCS_EXT_BGRX;
      components  = 4;

    } else {
      ARGUMENT_ERROR("Unsupportd :pixel_format option value.");
    }

    ptr->format                = format;
    ptr->out_color_space       = color_space;
    ptr->out_color_components  = components;
  }
}

static void
eval_decoder_opt_output_gamma(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FIXNUM:
  case T_FLOAT:
    ptr->output_gamma = NUM2DBL(opt);
    break;

  default:
    TYPE_ERROR("Unsupportd :output_gamma option value.");
    break;
  }
}

static void
eval_decoder_opt_do_fancy_upsampling(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    ptr->do_fancy_upsampling = (RTEST(opt))? TRUE: FALSE;
  }
}

static void
eval_decoder_opt_do_smoothing(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    ptr->do_block_smoothing = (RTEST(opt))? TRUE: FALSE;
  }
}

static void
eval_decoder_opt_dither( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE dmode;
  VALUE pass2;
  VALUE n_col;

  if (opt != Qundef) {
    if (TYPE(opt) != T_ARRAY) {
      TYPE_ERROR("Unsupportd :dither option value.");
    }

    if (RARRAY_LEN(opt) != 3) {
      ARGUMENT_ERROR(":dither is illeagal length (shall be 3 entries).");
    }

    dmode = RARRAY_AREF(opt, 0);
    pass2 = RARRAY_AREF(opt, 1);
    n_col = RARRAY_AREF(opt, 2);
    
    if (EQ_STR(dmode, "NONE")) {
      ptr->dither_mode     = JDITHER_NONE;
      ptr->quantize_colors = FALSE;

    } else if(EQ_STR(dmode, "ORDERED")) {
      ptr->dither_mode     = JDITHER_ORDERED;
      ptr->quantize_colors = TRUE;

    } else if(EQ_STR(dmode, "FS")) {
      ptr->dither_mode     = JDITHER_FS;
      ptr->quantize_colors = TRUE;

    } else {
      ARGUMENT_ERROR("dither mode is illeagal value.");
    }

    ptr->two_pass_quantize = (RTEST(pass2))? TRUE: FALSE;

    if (TYPE(n_col) == T_FIXNUM) {
      ptr->desired_number_of_colors = FIX2INT(n_col);

      if (ptr->desired_number_of_colors < 8 ||
          ptr->desired_number_of_colors > 256) {
        RANGE_ERROR("number of dithered colors shall be from 8 to 256.");
      } 

    } else {
      TYPE_ERROR("number of dithered colors is illeagal value.");
    }

    SET_FLAG(ptr, F_DITHER);
  }
}

#if 0
static void
eval_decoder_opt_use_1pass_quantizer(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      ptr->enable_1pass_quant = TRUE;
      ptr->buffered_image     = TRUE;
    } else {
      ptr->enable_1pass_quant = FALSE;
    }
  }
}

static void
eval_decoder_opt_use_external_colormap(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      ptr->enable_external_quant = TRUE;
      ptr->buffered_image        = TRUE;
    } else {
      ptr->enable_external_quant = FALSE;
    }
  }
}

static void
eval_decoder_opt_use_2pass_quantizer(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      ptr->enable_2pass_quant = TRUE;
      ptr->buffered_image     = TRUE;
    } else {
      ptr->enable_2pass_quant = FALSE;
    }
  }
}
#endif

static void
eval_decoder_opt_without_meta(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      CLR_FLAG(ptr, F_NEED_META);
    } else {
      SET_FLAG(ptr, F_NEED_META);
    }
  }
}

static void
eval_decoder_opt_expand_colormap(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      SET_FLAG(ptr, F_EXPAND_COLORMAP);
    } else {
      CLR_FLAG(ptr, F_EXPAND_COLORMAP);
    }
  }
}

static void
eval_decoder_opt_scale(jpeg_decode_t* ptr, VALUE opt)
{
  switch (TYPE(opt)) {
  case T_UNDEF:
    // Nothing
    break;

  case T_FLOAT:
    ptr->scale_num   = (int)(NUM2DBL(opt) * 1000.0);
    ptr->scale_denom = 1000;
    break;

  case T_RATIONAL:
    ptr->scale_num   = FIX2INT(rb_rational_num(opt));
    ptr->scale_denom = FIX2INT(rb_rational_den(opt));
    break;

  case T_ARRAY:
    if (RARRAY_LEN(opt) != 2) {
      ARGUMENT_ERROR("invalid length");
    }
    ptr->scale_num   = FIX2INT(RARRAY_AREF(opt, 0));
    ptr->scale_denom = FIX2INT(RARRAY_AREF(opt, 1));
    break;

  default:
    TYPE_ERROR("Unsupportd :exapnd_colormap option value.");
    break;
  }
}

static void
eval_decoder_opt_dct_method(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (EQ_STR(opt, "ISLOW")) {
      ptr->dct_method = JDCT_ISLOW;

    } else if (EQ_STR(opt, "IFAST")) {
      ptr->dct_method = JDCT_IFAST;

    } else if (EQ_STR(opt, "FLOAT")) {
      ptr->dct_method = JDCT_FLOAT;

    } else if (EQ_STR(opt, "FASTEST")) {
      ptr->dct_method = JDCT_FASTEST;

    } else {
      ARGUMENT_ERROR("Unsupportd :dct_method option value.");
    }
  }
}

static void
eval_decoder_opt_with_exif_tags(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      SET_FLAG(ptr, F_PARSE_EXIF);
    } else {
      CLR_FLAG(ptr, F_PARSE_EXIF);
    }
  }
}

static void
eval_decoder_opt_orientation(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef) {
    if (RTEST(opt)) {
      SET_FLAG(ptr, F_APPLY_ORIENTATION);
    } else {
      CLR_FLAG(ptr, F_APPLY_ORIENTATION);
    }
  }
}

static void
set_decoder_context( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE opts[N(decoder_opts_ids)];

  /*
   * parse options
   */
  rb_get_kwargs(opt, decoder_opts_ids, 0, N(decoder_opts_ids), opts);

  /*
   * set context
   */
  eval_decoder_opt_pixel_format(ptr, opts[0]);
  eval_decoder_opt_output_gamma(ptr, opts[1]);
  eval_decoder_opt_do_fancy_upsampling(ptr, opts[2]);
  eval_decoder_opt_do_smoothing(ptr, opts[3]);
  eval_decoder_opt_dither(ptr, opts[4]);
#if 0
  eval_decoder_opt_use_1pass_quantizer(ptr, opts[5]);
  eval_decoder_opt_use_external_colormap(ptr, opts[6]);
  eval_decoder_opt_use_2pass_quantizer(ptr, opts[7]);
#endif
  eval_decoder_opt_without_meta(ptr, opts[5]);
  eval_decoder_opt_expand_colormap(ptr, opts[6]);
  eval_decoder_opt_scale(ptr, opts[7]);
  eval_decoder_opt_dct_method(ptr, opts[8]);
  eval_decoder_opt_with_exif_tags(ptr, opts[9]);
  eval_decoder_opt_with_exif_tags(ptr, opts[10]);
  eval_decoder_opt_orientation(ptr, opts[11]);
}

/**
 * initialize decoder object
 *
 * @overload initialize(opts)
 *
 *   @param opts [Hash] options to initialize object
 *
 *   @option opts [Symbol] :pixel_format
 *     specifies the format of the output image. possible values are:
 *     YUV422 YUYV RGB565 RGB RGB24 BGR BGR24 YUV444 YCbCr
 *     RGBX RGB32 BGRX BGR32 GRAYSCALE
 *
 *   @option opts [Float] :output_gamma
 *
 *   @option opts [Boolean] :fancy_upsampling
 *
 *   @option opts [Boolean] :do_smoothing
 *
 *   @option opts [Array] :dither
 *     specifies dithering parameters. A 3-elements array.
 *     specify the dither type as a string for the 1st element,
 *     whether to use 2-pass quantize for the 2nd element as a boolean,
 *     and the number of colors used for the 3rd element as an integer
 *     from 16 to 256.
 *
 *   @option opts [Boolean] :without_meta
 *     specifies whether or not to include meta information in the
 *     output data. If true, no meta-information is output.
 *
 *   @option opts [Boolean] :expand_colormap
 *     specifies whether to expand the color map. If dither is specified,
 *     the output will be a color number of 1 byte per pixel, but if this
 *     option is set to true, the output will be expanded to color information.
 *
 *   @option opts [Ratioanl] :scale
 *
 *   @option opts [Symbol] :dct_method
 *     specifies how decoding is handled. possible values are:
 *     FASTEST ISLOW IFAST FLOAT
 *
 *   @option opts [Boolean] :with_exif_tags
 *     specifies whether to include Exif tag information in the output data.
 *     set this option to true to parse the Exif tag information and include
 *     it in the meta information output.
 *
 *   @option opts [Boolean] :with_exif
 *     alias to :with_exif_tags option.
 */
static VALUE
rb_decoder_initialize( int argc, VALUE *argv, VALUE self)
{
  jpeg_decode_t* ptr;
  VALUE opt;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * parse arguments
   */
  rb_scan_args( argc, argv, "01", &opt);

  if (opt != Qnil) Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  set_decoder_context(ptr, opt);

  return Qtrue;
}

static VALUE
rb_decoder_set(VALUE self, VALUE opt)
{
  jpeg_decode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * check argument
   */
  Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  set_decoder_context(ptr, opt);

  return Qtrue;
}

static VALUE
get_colorspace_str( J_COLOR_SPACE cs)
{
  const char* cstr;

  switch (cs) {
  case JCS_GRAYSCALE:
    cstr = "GRAYSCALE";
    break;

  case JCS_RGB:
    cstr = "RGB";
    break;

  case JCS_YCbCr:
    cstr = "YCbCr";
    break;

  case JCS_CMYK:
    cstr = "CMYK";
    break;

  case JCS_YCCK:
    cstr = "YCCK";
    break;
#if JPEG_LIB_VERSION < 90
  case JCS_EXT_RGB:
    cstr = "RGB";
    break;

  case JCS_EXT_RGBX:
    cstr = "RGBX";
    break;

  case JCS_EXT_BGR:
    cstr = "BGR";
    break;

  case JCS_EXT_BGRX:
    cstr = "BGRX";
    break;

  case JCS_EXT_XBGR:
    cstr = "XBGR";
    break;

  case JCS_EXT_XRGB:
    cstr = "XRGB";
    break;

  case JCS_EXT_RGBA:
    cstr = "RGBA";
    break;

  case JCS_EXT_BGRA:
    cstr = "BGRA";
    break;

  case JCS_EXT_ABGR:
    cstr = "ABGR";
    break;

  case JCS_EXT_ARGB:
    cstr = "ARGB";
    break;
#endif /* JPEG_LIB_VERSION < 90 */

  default:
    cstr = "UNKNOWN";
    break;
  }

  return rb_str_new_cstr(cstr);
}

typedef struct {
  int be;
  uint8_t* head;
  uint8_t* cur;
  size_t size;

  struct {
    tag_entry_t* tbl;
    size_t n;
  } tags;

  int next;
} exif_t;

static uint16_t
get_u16(uint8_t* src, int be)
{
  uint16_t ret;

  if (be) {
    ret = (((src[0] << 8) & 0xff00)|
           ((src[1] << 0) & 0x00ff));
  } else {
    ret = (((src[1] << 8) & 0xff00)|
           ((src[0] << 0) & 0x00ff));
  }

  return ret;
}

/*
static int16_t
get_s16(uint8_t* src, int be)
{
  int16_t ret;

  if (be) {
    ret = (((src[0] << 8) & 0xff00)|
           ((src[1] << 0) & 0x00ff));
  } else {
    ret = (((src[1] << 8) & 0xff00)|
           ((src[0] << 0) & 0x00ff));
  }

  return ret;
}
*/

static uint32_t
get_u32(uint8_t* src, int be)
{
  uint32_t ret;

  if (be) {
    ret = (((src[0] << 24) & 0xff000000)|
           ((src[1] << 16) & 0x00ff0000)|
           ((src[2] <<  8) & 0x0000ff00)|
           ((src[3] <<  0) & 0x000000ff));
  } else {
    ret = (((src[3] << 24) & 0xff000000)|
           ((src[2] << 16) & 0x00ff0000)|
           ((src[1] <<  8) & 0x0000ff00)|
           ((src[0] <<  0) & 0x000000ff));
  }

  return ret;
}

static int32_t
get_s32(uint8_t* src, int be)
{
  int32_t ret;

  if (be) {
    ret = (((src[0] << 24) & 0xff000000)|
           ((src[1] << 16) & 0x00ff0000)|
           ((src[2] <<  8) & 0x0000ff00)|
           ((src[3] <<  0) & 0x000000ff));
  } else {
    ret = (((src[3] << 24) & 0xff000000)|
           ((src[2] << 16) & 0x00ff0000)|
           ((src[1] <<  8) & 0x0000ff00)|
           ((src[0] <<  0) & 0x000000ff));
  }

  return ret;
}

static void
exif_increase(exif_t* ptr, size_t size)
{
  ptr->cur  += size;
  ptr->size -= size;
}

static void
exif_init(exif_t* ptr, uint8_t* src, size_t size)
{
  int be;
  uint16_t ident;
  uint32_t off;

  /*
   * Check Exif identifier
   */
  if (memcmp(src, "Exif\0\0", 6)) {
    rb_raise(decerr_klass, "invalid exif identifier");
  }

  /*
   * Check TIFF header and judge endian
   */
  do {
    if (!memcmp(src + 6, "MM", 2)) {
      be = !0;
      break;
    }

    if (!memcmp(src + 6, "II", 2)) {
      be = 0;
      break;
    }

    rb_raise(decerr_klass, "invalid tiff header");
  } while (0);

  /*
   * Check TIFF identifier
   */
  ident = get_u16(src + 8, be);
  if (ident != 0x002a) {
    rb_raise(decerr_klass, "invalid tiff identifier");
  }

  /*
   * get offset for 0th IFD
   */
  off = get_u32(src + 10, be);
  if (off < 8 || off >= size - 6) {
    rb_raise(decerr_klass, "invalid offset dentifier");
  }

  /*
   * initialize Exif context
   */
  ptr->be       = be;
  ptr->head     = src + 6;
  ptr->cur      = ptr->head + off;
  ptr->size     = size - (6 + off);
  ptr->tags.tbl = tag_tiff;
  ptr->tags.n   = N(tag_tiff);
  ptr->next     = 0;
}

static void
exif_fetch_tag_header(exif_t* ptr, uint16_t* tag, uint16_t* type)
{
  *tag  = get_u16(ptr->cur + 0, ptr->be);
  *type = get_u16(ptr->cur + 2, ptr->be);
}


static void
exif_fetch_byte_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    obj = INT2FIX(*p);
    break;

  default:
    p = ptr->head + get_u32(p, ptr->be);

  case 2:
  case 3:
  case 4:
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      rb_ary_push(obj, INT2FIX(p[i]));
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_ascii_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  if (n > 4) {
    p = ptr->head + get_u32(p, ptr->be);
  }

  obj = rb_utf8_str_new((char*)p, n);
  rb_funcall(obj, rb_intern("strip!"), 0);

  *dst = obj;
}

static void
exif_fetch_short_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    obj = INT2FIX(get_u16(p, ptr->be));
    break;

  default:
    p = ptr->head + get_u32(p, ptr->be);

  case 2:
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      rb_ary_push(obj, INT2FIX(get_u16(p, ptr->be)));
      p += 2;
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_long_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    obj = INT2FIX(get_u32(p, ptr->be));
    break;

  default:
    p   = ptr->head + get_u32(p, ptr->be);
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      rb_ary_push(obj, INT2FIX(get_u32(p, ptr->be)));
      p += 4;
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_rational_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;
  uint32_t deno;
  uint32_t num;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->head + get_u32(ptr->cur + 8, ptr->be);

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    num  = get_u32(p + 0, ptr->be);
    deno = get_u32(p + 4, ptr->be);
    if (num == 0 && deno == 0) {
      deno = 1;
    }
    obj = rb_rational_new(INT2FIX(num), INT2FIX(deno));
    break;

  default:
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      num  = get_u32(p + 0, ptr->be);
      deno = get_u32(p + 4, ptr->be);
      if (num == 0 && deno == 0) {
        deno = 1;
      }
      rb_ary_push(obj, rb_rational_new(INT2FIX(num), INT2FIX(deno)));
      p += 8;
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_undefined_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  if (n > 4) {
    p = ptr->head + get_u32(p, ptr->be);
  }

  obj = rb_enc_str_new((char*)p, n, rb_ascii8bit_encoding());

  *dst = obj;
}

static void
exif_fetch_slong_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->cur + 8;

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    obj = INT2FIX(get_s32(p, ptr->be));
    break;

  default:
    p   = ptr->head + get_u32(p, ptr->be);
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      rb_ary_push(obj, INT2FIX(get_s32(p, ptr->be)));
      p += 4;
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_srational_data(exif_t* ptr, VALUE* dst)
{
  VALUE obj;

  int i;
  uint32_t n;
  uint8_t* p;
  uint32_t deno;
  uint32_t num;

  n = get_u32(ptr->cur + 4, ptr->be);
  p = ptr->head + get_u32(ptr->cur + 8, ptr->be);

  switch (n) {
  case 0:
    obj = Qnil;
    break;

  case 1:
    num  = get_s32(p + 0, ptr->be);
    deno = get_s32(p + 4, ptr->be);
    if (num == 0 && deno == 0) {
      deno = 1;
    }
    obj = rb_rational_new(INT2FIX(num), INT2FIX(deno));
    break;

  default:
    obj = rb_ary_new_capa(n);
    for (i = 0; i < (int)n; i++) {
      num  = get_s32(p + 0, ptr->be);
      deno = get_s32(p + 4, ptr->be);
      if (num == 0 && deno == 0) {
        deno = 1;
      }
      rb_ary_push(obj, rb_rational_new(INT2FIX(num), INT2FIX(deno)));
      p += 8;
    }
    break;
  }

  *dst = obj;
}

static void
exif_fetch_child_ifd(exif_t* ptr, tag_entry_t* tbl, size_t n, exif_t* dst)
{
  uint32_t off;

  off = get_u32(ptr->cur + 8, ptr->be); 

  dst->be       = ptr->be;
  dst->head     = ptr->head;
  dst->cur      = ptr->head + off;
  dst->size     = ptr->size - off;
  dst->tags.tbl = tbl;
  dst->tags.n   = n;
  dst->next     = 0;
}

static int
exif_read(exif_t* ptr, VALUE dst)
{
  int ret;
  int i;
  uint16_t ntag;
  uint16_t tag;
  uint16_t type;
  uint32_t off;

  exif_t child;

  VALUE key;
  VALUE val;

  ntag = get_u16(ptr->cur, ptr->be);
  exif_increase(ptr, 2);

  for (i = 0; i < ntag; i++) {
    exif_fetch_tag_header(ptr, &tag, &type);

    switch (tag) {
    case 34665: // ExifIFDPointer
      key = ID2SYM(rb_intern("exif"));
      val = rb_hash_new();

      exif_fetch_child_ifd(ptr, tag_exif, N(tag_exif), &child);
      exif_read(&child, val);
      break;

    case 34853: // GPSInfoIFDPointer
      key = ID2SYM(rb_intern("gps"));
      val = rb_hash_new();

      exif_fetch_child_ifd(ptr, tag_gps, N(tag_gps), &child);
      exif_read(&child, val);
      break;

    case 40965: // InteroperabilityIFDPointer
      key = ID2SYM(rb_intern("interoperability"));
      val = rb_hash_new();

      exif_fetch_child_ifd(ptr, tag_i14y, N(tag_i14y), &child);
      exif_read(&child, val);
      break;

    default:
      key = lookup_tag_symbol(ptr->tags.tbl, ptr->tags.n, tag);

      switch (type) {
      case 1:  // when BYTE
        exif_fetch_byte_data(ptr, &val);
        break;

      case 2:  // when ASCII
        exif_fetch_ascii_data(ptr, &val);
        break;

      case 3:  // when SHORT
        exif_fetch_short_data(ptr, &val);
        break;

      case 4:  // when LONG
        exif_fetch_long_data(ptr, &val);
        break;

      case 5:  // when RATIONAL
        exif_fetch_rational_data(ptr, &val);
        break;

      case 7:  // when UNDEFINED
        exif_fetch_undefined_data(ptr, &val);
        break;

      case 9:  // when SLONG
        exif_fetch_slong_data(ptr, &val);
        break;

      case 10: // when SRATIONAL
        exif_fetch_srational_data(ptr, &val);
        break;

      default:
        rb_raise(decerr_klass, "invalid tag data type");
      }
    }

    rb_hash_aset(dst, key, val);
    exif_increase(ptr, 12);
  }

  off = get_u32(ptr->cur, ptr->be);
  if (off != 0) {
    ptr->cur  = ptr->head + off;
    ptr->next = !0;
  }

  return ret;
}

#define THUMBNAIL_OFFSET    ID2SYM(rb_intern("jpeg_interchange_format"))
#define THUMBNAIL_SIZE      ID2SYM(rb_intern("jpeg_interchange_format_length"))

static VALUE
create_exif_tags_hash(jpeg_decode_t* ptr)
{
  VALUE ret;
  jpeg_saved_marker_ptr marker;
  exif_t exif;

  ret = rb_hash_new();

  for (marker = ptr->cinfo.marker_list;
            marker != NULL; marker = marker->next) {

    if (marker->data_length < 14) continue;
    if (memcmp(marker->data, "Exif\0\0", 6)) continue;

    /* 0th IFD */
    exif_init(&exif, marker->data, marker->data_length);
    exif_read(&exif, ret);

    if (exif.next) {
      /* when 1th IFD (tumbnail) exist */
      VALUE info;
      VALUE off;
      VALUE size;
      VALUE data;

      info = rb_hash_new();

      exif_read(&exif, info);

      off  = rb_hash_lookup(info, THUMBNAIL_OFFSET);
      size = rb_hash_lookup(info, THUMBNAIL_SIZE);

      if (TYPE(off) == T_FIXNUM && TYPE(size) == T_FIXNUM) {
        data = rb_enc_str_new((char*)exif.head + FIX2INT(off),
                              FIX2INT(size), rb_ascii8bit_encoding());

        rb_hash_lookup(info, THUMBNAIL_OFFSET);
        rb_hash_lookup(info, THUMBNAIL_SIZE);
        rb_hash_aset(info, ID2SYM(rb_intern("jpeg_interchange")), data);
        rb_hash_aset(ret, ID2SYM(rb_intern("thumbnail")), info);
      }
    }
    break;
  }

  return ret;
}

static void
pick_exif_orientation(jpeg_decode_t* ptr)
{
  jpeg_saved_marker_ptr marker;
  int o9n;
  uint8_t* p;
  int be;
  uint32_t off;
  int i;
  int n;

  o9n = 0;

  for (marker = ptr->cinfo.marker_list;
            marker != NULL; marker = marker->next) {

    if (marker->data_length < 14) continue;

    p = marker->data;

    /*
     * check Exif identifier
     */
    if (memcmp(p, "Exif\0\0", 6)) continue;

    /*
     * check endian marker
     */
    if (!memcmp(p + 6, "MM", 2)) {
      be = !0;

    } else if (!memcmp(p + 6, "II", 2)) {
      be = 0;

    } else {
      continue;
    }

    /*
     * check TIFF identifier
     */
    if (get_u16(p + 8, be) != 0x002a) continue;

    /*
     * set 0th IFD address
     */
    off = get_u32(p + 10, be);
    if (off < 8 || off >= marker->data_length - 6) continue;

    p += (6 + off);

    /* ここまでくればAPP1がExifタグなので
     * 0th IFDをなめてOrientationタグを探す */

    n = get_u16(p, be);
    p += 2;

    for (i = 0; i < n; i++) {
      int tag;
      int type;
      int num;

      tag  = get_u16(p + 0, be);
      type = get_u16(p + 2, be);
      num  = get_u32(p + 4, be);

      if (tag == 0x0112) {
        if (type == 3 && num == 1) {
          o9n = get_u16(p + 8, be);
          goto loop_out;

        } else {
          fprintf(stderr,
                  "Illeagal orientation tag found [type:%d, num:%d]\n",
                  type,
                  num);
        }
      }

      p += 12;
    }
  }
  loop_out:

  ptr->orientation.value = (o9n >= 1 && o9n <= 8)? (o9n - 1): 0;
}

static VALUE
create_colormap(jpeg_decode_t* ptr)
{
  VALUE ret;
  struct jpeg_decompress_struct* cinfo;
  JSAMPARRAY map;
  int i;   // volatileを外すとaarch64のgcc6でクラッシュする場合がある
  uint32_t c;

  cinfo = &ptr->cinfo;
  ret   = rb_ary_new_capa(cinfo->actual_number_of_colors);
  map   = cinfo->colormap;

  switch (cinfo->out_color_components) {
  case 1:
    for (i = 0; i < cinfo->actual_number_of_colors; i++) {
      c = map[0][i];
      rb_ary_push(ret, INT2FIX(c));
    }
    break;

  case 2:
    for (i = 0; i < cinfo->actual_number_of_colors; i++) {
      c = (map[0][i] << 8) | (map[1][i] << 0);
      rb_ary_push(ret, INT2FIX(c));
    }
    break;

  case 3:
    for (i = 0; i < cinfo->actual_number_of_colors; i++) {
      c = (map[0][i] << 16) | (map[1][i] << 8) | (map[2][i] << 0);

      rb_ary_push(ret, INT2FIX(c));
    }
    break;

  default:
    RUNTIME_ERROR("this number of components is not implemented yet");
  }

  return ret;
}

static VALUE
rb_meta_exif_tags(VALUE self)
{
  return rb_ivar_get(self, id_exif_tags);
}

static VALUE
create_meta(jpeg_decode_t* ptr)
{
  VALUE ret;
  struct jpeg_decompress_struct* cinfo;
  int width;
  int height;
  int stride;

  ret    = rb_obj_alloc(meta_klass);
  cinfo  = &ptr->cinfo;

  if (TEST_FLAG(ptr, F_APPLY_ORIENTATION) && (ptr->orientation.value & 4)) {
    width  = cinfo->output_height;
    height = cinfo->output_width;
  } else {
    width  = cinfo->output_width;
    height = cinfo->output_height;
  }

  stride = cinfo->output_width * cinfo->output_components;

  rb_ivar_set(ret, id_width, INT2FIX(width));
  rb_ivar_set(ret, id_stride, INT2FIX(stride));
  rb_ivar_set(ret, id_height, INT2FIX(height));

  rb_ivar_set(ret, id_orig_cs, get_colorspace_str(cinfo->jpeg_color_space));

  if (ptr->format == FMT_YVU) {
    rb_ivar_set(ret, id_out_cs, rb_str_new_cstr("YCrCb"));
  } else {
    rb_ivar_set(ret, id_out_cs, get_colorspace_str(cinfo->out_color_space));
  }

  if (TEST_FLAG_ALL(ptr, F_DITHER | F_EXPAND_COLORMAP)) {
    rb_ivar_set(ret, id_ncompo, INT2FIX(cinfo->out_color_components));
  } else {
    rb_ivar_set(ret, id_ncompo, INT2FIX(cinfo->output_components));
  }

  if (TEST_FLAG(ptr, F_PARSE_EXIF)) {
    rb_ivar_set(ret, id_exif_tags, create_exif_tags_hash(ptr));
    rb_define_singleton_method(ret, "exif_tags", rb_meta_exif_tags, 0);
    rb_define_singleton_method(ret, "exif", rb_meta_exif_tags, 0);
  } 

  if (TEST_FLAG(ptr, F_DITHER)) {
    rb_ivar_set(ret, id_colormap, create_colormap(ptr));
  }
  
  return ret;
}

static VALUE
do_read_header(jpeg_decode_t* ptr, uint8_t* jpg, size_t jpg_sz)
{
  VALUE ret;

  switch (ptr->format) {
  case FMT_YUV422:
  case FMT_RGB565:
    NOT_IMPLEMENTED_ERROR( "not implemented colorspace");
    break;
  }

  jpeg_create_decompress(&ptr->cinfo);

  ptr->cinfo.err                   = jpeg_std_error(&ptr->err_mgr.jerr);
  ptr->err_mgr.jerr.output_message = decode_output_message;
  ptr->err_mgr.jerr.emit_message   = decode_emit_message;
  ptr->err_mgr.jerr.error_exit     = decode_error_exit;

  ptr->cinfo.raw_data_out          = FALSE;
  ptr->cinfo.dct_method            = JDCT_FLOAT;

  if (setjmp(ptr->err_mgr.jmpbuf)) {
    jpeg_destroy_decompress(&ptr->cinfo);
    rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);
  } else {
    jpeg_mem_src(&ptr->cinfo, jpg, jpg_sz);

    if (TEST_FLAG(ptr, F_PARSE_EXIF | F_APPLY_ORIENTATION)) {
      jpeg_save_markers(&ptr->cinfo, JPEG_APP1, 0xFFFF);
    }

    jpeg_read_header(&ptr->cinfo, TRUE);
    jpeg_calc_output_dimensions(&ptr->cinfo);

    if (TEST_FLAG(ptr, F_APPLY_ORIENTATION)) {
      pick_exif_orientation(ptr);
    }

    ret = create_meta(ptr);

    jpeg_destroy_decompress(&ptr->cinfo);
  }

  return ret;
}

/**
 * read meta data
 *
 * @overload read_header(jpeg)
 *
 *   @param jpeg [String] input data.
 *
 *   @return [JPEG::Meta] metadata.
 */
static VALUE
rb_decoder_read_header(VALUE self, VALUE data)
{
  VALUE ret;
  jpeg_decode_t* ptr;

  /*
   * initialize
   */
  Data_Get_Struct(self, jpeg_decode_t, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  /*
   * do encode
   */
  ret = do_read_header(ptr, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));

  return ret;
}

static VALUE
rb_decode_result_meta(VALUE self)
{
  return rb_ivar_get(self, id_meta);
}

static void
add_meta(VALUE obj, jpeg_decode_t* ptr)
{
  VALUE meta;

  meta = create_meta(ptr);

  rb_ivar_set(obj, id_meta, meta);
  rb_define_singleton_method(obj, "meta", rb_decode_result_meta, 0);
}

static VALUE
expand_colormap(struct jpeg_decompress_struct* cinfo, uint8_t* src)
{
  /*
   * 本関数はcinfo->out_color_componentsが1または3であることを前提に
   * 作成されています。
   */

  VALUE ret;
  volatile int i;   // volatileを外すとaarch64のgcc6でクラッシュする場合がある
  int n;
  uint8_t* dst;
  JSAMPARRAY map;

  n   = cinfo->output_width * cinfo->output_height;
  ret = rb_str_buf_new(n * cinfo->out_color_components);
  dst = (uint8_t*)RSTRING_PTR(ret);
  map = cinfo->colormap;

  switch (cinfo->out_color_components) {
  case 1:
    for (i = 0; i < n; i++) {
      dst[i] = map[0][src[i]];
    }
    break;

  case 2:
    for (i = 0; i < n; i++) {
      dst[0] = map[0][src[i]];
      dst[1] = map[1][src[i]];

      dst += 2;
    }
    break;

  case 3:
    for (i = 0; i < n; i++) {
      dst[0] = map[0][src[i]];
      dst[1] = map[1][src[i]];
      dst[2] = map[2][src[i]];

      dst += 3;
    }
    break;

  default:
    RUNTIME_ERROR("this number of components is not implemented yet");
  }

  rb_str_set_len(ret, n * cinfo->out_color_components);

  return ret;
}

static void
swap_cbcr(uint8_t* p, size_t size)
{
  int i;
  uint8_t tmp;

  for (i = 0; i < (int)size; i++) {
    tmp  = p[1];
    p[1] = p[2];
    p[2] = tmp;
  }
}

static void
do_transpose8(uint8_t* img, int wd, int ht, void* dst)
{
  int x;
  int y;

  uint8_t* sp;
  uint8_t* dp;

  sp = (uint8_t*)img;

  for (y = 0; y < ht; y++) {
    dp = (uint8_t*)dst + y;

    for (x = 0; x < wd; x++) {
      *dp = *sp;

      sp++;
      dp += ht;
    }
  }
}

static void
do_transpose16(void* img, int wd, int ht, void* dst)
{
  int x;
  int y;

  uint16_t* sp;
  uint16_t* dp;

  sp = (uint16_t*)img;

  for (y = 0; y < ht; y++) {
    dp = (uint16_t*)dst + y;

    for (x = 0; x < wd; x++) {
      *dp = *sp;

      sp++;
      dp += ht;
    }
  }
}

static void
do_transpose24(void* img, int wd, int ht, void* dst)
{
  int x;
  int y;
  int st;

  uint8_t* sp;
  uint8_t* dp;

  sp = (uint8_t*)img;
  st = ht * 3;

  for (y = 0; y < ht; y++) {
    dp = (uint8_t*)dst + (y * 3);

    for (x = 0; x < wd; x++) {
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];

      sp += 3;
      dp += st;
    }
  }
}

static void
do_transpose32(void* img, int wd, int ht, void* dst)
{
  int x;
  int y;

  uint32_t* sp;
  uint32_t* dp;

  sp = (uint32_t*)img;

  for (y = 0; y < ht; y++) {
    dp = (uint32_t*)dst + y;

    for (x = 0; x < wd; x++) {
      *dp = *sp;

      sp++;
      dp += ht;
    }
  }
}

static void
do_transpose(void* img, int wd, int ht, int nc, void* dst)
{
  switch (nc) {
  case 1:
    do_transpose8(img, wd, ht, dst);
    break;

  case 2:
    do_transpose16(img, wd, ht, dst);
    break;

  case 3:
    do_transpose24(img, wd, ht, dst);
    break;

  case 4:
    do_transpose32(img, wd, ht, dst);
    break;
  }
}

static void
do_upside_down8(void* img, int wd, int ht)
{
  uint8_t* sp;
  uint8_t* dp;

  sp = (uint8_t*)img;
  dp = (uint8_t*)img + ((wd * ht) - 1);

  while (sp < dp) {
    SWAP(*sp, *dp, uint8_t);

    sp++;
    dp--;
  }
}

static void
do_upside_down16(void* img, int wd, int ht)
{
  uint16_t* sp;
  uint16_t* dp;

  sp = (uint16_t*)img;
  dp = (uint16_t*)img + ((wd * ht) - 1);

  while (sp < dp) {
    SWAP(*sp, *dp, uint8_t);

    sp++;
    dp--;
  }
}

static void
do_upside_down24(void* img, int wd, int ht)
{
  uint8_t* sp;
  uint8_t* dp;

  sp = (uint8_t*)img;
  dp = (uint8_t*)img + ((wd * ht * 3) - 3);

  while (sp < dp) {
    SWAP(sp[0], dp[0], uint8_t);
    SWAP(sp[1], dp[1], uint8_t);
    SWAP(sp[2], dp[2], uint8_t);

    sp += 3;
    dp -= 3;
  }
}

static void
do_upside_down32(void* img, int wd, int ht)
{
  uint32_t* sp;
  uint32_t* dp;

  sp = (uint32_t*)img;
  dp = (uint32_t*)img + ((wd * ht) - 1);

  ht /= 2;

  while (sp < dp) {
    SWAP(*sp, *dp, uint32_t);

    sp++;
    dp--;
  }
}

static void
do_upside_down(void* img, int wd, int ht, int nc)
{
  switch (nc) {
  case 1:
    do_upside_down8(img, wd, ht);
    break;

  case 2:
    do_upside_down16(img, wd, ht);
    break;

  case 3:
    do_upside_down24(img, wd, ht);
    break;

  case 4:
    do_upside_down32(img, wd, ht);
    break;
  }
}

static void
do_flip_horizon8(void* img, int wd, int ht)
{
  int y;
  int st;

  uint8_t* sp;
  uint8_t* dp;

  st  = wd;
  wd /= 2;

  sp = (uint8_t*)img;
  dp = (uint8_t*)img + (st - 1);

  for (y = 0; y < ht; y++) {
    while (sp < dp) {
      SWAP(*sp, *dp, uint8_t);

      sp++;
      dp--;
    }

    sp = sp - wd + st;
    dp = sp + (st - 1);
  }
}

static void
do_flip_horizon16(void* img, int wd, int ht)
{
  int y;
  int st;

  uint16_t* sp;
  uint16_t* dp;

  st  = wd;
  wd /= 2;

  sp = (uint16_t*)img;
  dp = (uint16_t*)img + (st - 1);

  for (y = 0; y < ht; y++) {
    while (sp < dp) {
      SWAP(*sp, *dp, uint16_t);

      sp++;
      dp--;
    }

    sp = sp - wd + st;
    dp = sp + (st - 1);
  }
}

static void
do_flip_horizon24(void* img, int wd, int ht)
{
  int y;
  int st;

  uint8_t* sp;
  uint8_t* dp;

  st  = wd * 3;
  wd /= 2;

  sp = (uint8_t*)img;
  dp = (uint8_t*)img + (st - 3);

  for (y = 0; y < ht; y++) {
    while (sp < dp) {
      SWAP(sp[0], dp[0], uint8_t);
      SWAP(sp[1], dp[1], uint8_t);
      SWAP(sp[2], dp[2], uint8_t);

      sp += 3;
      dp -= 3;
    }

    sp = (sp - (wd * 3)) + st;
    dp = sp + (st - 3);
  }
}

static void
do_flip_horizon32(void* img, int wd, int ht)
{
  int y;
  int st;

  uint32_t* sp;
  uint32_t* dp;

  st  = wd;
  wd /= 2;

  sp = (uint32_t*)img;
  dp = (uint32_t*)img + (st - 1);

  for (y = 0; y < ht; y++) {
    while (sp < dp) {
      SWAP(*sp, *dp, uint32_t);

      sp++;
      dp--;
    }

    sp = sp - wd + st;
    dp = sp + (st - 1);
  }
}

static void
do_flip_horizon(void* img, int wd, int ht, int nc)
{
  switch (nc) {
  case 1:
    do_flip_horizon8(img, wd, ht);
    break;

  case 2:
    do_flip_horizon16(img, wd, ht);
    break;

  case 3:
    do_flip_horizon24(img, wd, ht);
    break;

  case 4:
    do_flip_horizon32(img, wd, ht);
    break;
  }
}

static VALUE
shift_orientation_buffer(jpeg_decode_t* ptr, VALUE img)
{
  VALUE ret;
  int len;

  ret = ptr->orientation.buf;
  len = RSTRING_LEN(img);

  if (ret == Qnil || RSTRING_LEN(ret) != len) {
    ret = rb_str_buf_new(len);
    rb_str_set_len(ret, len);
  }

  ptr->orientation.buf = img;

  return ret;
}

static VALUE
apply_orientation(jpeg_decode_t* ptr, VALUE img)
{
  struct jpeg_decompress_struct* cinfo;
  int wd;
  int ht;
  int nc;
  VALUE tmp;

  cinfo = &ptr->cinfo;
  wd    = cinfo->output_width;
  ht    = cinfo->output_height;
  nc    = cinfo->output_components;

  if (ptr->orientation.value & 4) {
    /* 転置は交換アルゴリズムでは実装できないので新規バッファを
       用意する */
    tmp = img;
    img = shift_orientation_buffer(ptr, tmp);
    SWAP(wd, ht, int);

    do_transpose(RSTRING_PTR(tmp), ht, wd, nc, RSTRING_PTR(img)); 
  }

  if (ptr->orientation.value & 2) {
    do_upside_down(RSTRING_PTR(img), wd, ht, nc); 
  }

  if (ptr->orientation.value & 1) {
    do_flip_horizon(RSTRING_PTR(img), wd, ht, nc); 
  }

  return img;
}

static VALUE
do_decode(jpeg_decode_t* ptr, uint8_t* jpg, size_t jpg_sz)
{
  VALUE ret;
  struct jpeg_decompress_struct* cinfo;
  JSAMPARRAY array;

  size_t stride;
  size_t raw_sz;
  uint8_t* raw;
  int i;
  int j;

  ret   = Qundef; // warning対策
  cinfo = &ptr->cinfo;
  array = ALLOC_ARRAY();
                  
  switch (ptr->format) {
  case FMT_YUV422:
  case FMT_RGB565:
    NOT_IMPLEMENTED_ERROR( "not implemented colorspace");
    break;

  case FMT_GRAYSCALE:
  case FMT_YUV:
  case FMT_RGB:
  case FMT_BGR:
  case FMT_YVU:
  case FMT_RGB32:
  case FMT_BGR32:
    jpeg_create_decompress(cinfo);

    cinfo->err                       = jpeg_std_error(&ptr->err_mgr.jerr);
    ptr->err_mgr.jerr.output_message = decode_output_message;
    ptr->err_mgr.jerr.emit_message   = decode_emit_message;
    ptr->err_mgr.jerr.error_exit     = decode_error_exit;

    if (setjmp(ptr->err_mgr.jmpbuf)) {
      jpeg_abort_decompress(cinfo);
      jpeg_destroy_decompress(&ptr->cinfo);

      free(array);
      rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);

    } else {
      jpeg_mem_src(cinfo, jpg, jpg_sz);

      if (TEST_FLAG(ptr, F_PARSE_EXIF | F_APPLY_ORIENTATION)) {
        jpeg_save_markers(&ptr->cinfo, JPEG_APP1, 0xFFFF);
      }

      jpeg_read_header(cinfo, TRUE);

      cinfo->raw_data_out              = FALSE;
      cinfo->dct_method                = ptr->dct_method;

      cinfo->out_color_space           = ptr->out_color_space;
      cinfo->out_color_components      = ptr->out_color_components;
      cinfo->scale_num                 = ptr->scale_num;
      cinfo->scale_denom               = ptr->scale_denom;
      cinfo->output_gamma              = ptr->output_gamma;
      cinfo->do_fancy_upsampling       = ptr->do_fancy_upsampling;
      cinfo->do_block_smoothing        = ptr->do_block_smoothing;
      cinfo->quantize_colors           = ptr->quantize_colors;
      cinfo->dither_mode               = ptr->dither_mode;
      cinfo->two_pass_quantize         = ptr->two_pass_quantize;
      cinfo->desired_number_of_colors  = ptr->desired_number_of_colors;
      cinfo->enable_1pass_quant        = ptr->enable_1pass_quant;
      cinfo->enable_external_quant     = ptr->enable_external_quant;
      cinfo->enable_2pass_quant        = ptr->enable_2pass_quant;

      jpeg_calc_output_dimensions(cinfo);
      jpeg_start_decompress(cinfo);

      stride = cinfo->output_components * cinfo->output_width;
      raw_sz = stride * cinfo->output_height;
      ret    = rb_str_buf_new(raw_sz);
      raw    = (uint8_t*)RSTRING_PTR(ret);

      while (cinfo->output_scanline < cinfo->output_height) {
        for (i = 0, j = cinfo->output_scanline; i < UNIT_LINES; i++, j++) {
          array[i] = raw + (j * stride);
        }

        jpeg_read_scanlines(cinfo, array, UNIT_LINES);
      }

      if (TEST_FLAG(ptr, F_EXPAND_COLORMAP) && IS_COLORMAPPED(cinfo)) {
        ret = expand_colormap(cinfo, raw);
      } else {
        rb_str_set_len(ret, raw_sz);
      }

      if (ptr->format == FMT_YVU) swap_cbcr(raw, raw_sz);

      if (TEST_FLAG(ptr, F_APPLY_ORIENTATION)) {
        pick_exif_orientation(ptr);
        ret = apply_orientation(ptr, ret);
      }

      if (TEST_FLAG(ptr, F_NEED_META)) add_meta(ret, ptr);

      jpeg_finish_decompress(cinfo);
      jpeg_destroy_decompress(&ptr->cinfo);
    }
    break;
  }

  free(array);

  return ret;
}

/**
 * decode JPEG data
 *
 * @overload decode(jpeg)
 *
 *   @param jpeg [String]  JPEG data to decode.
 *
 *   @return [String] decoded raw image data.
 */
static VALUE
rb_decoder_decode(VALUE self, VALUE data)
{
    VALUE ret;
    jpeg_decode_t* ptr;

    /*
     * initialize
     */
    Data_Get_Struct(self, jpeg_decode_t, ptr);

    /*
     * argument check
     */
    Check_Type(data, T_STRING);

    /*
     * do encode
     */
    ret = do_decode(ptr, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));

    return ret;
}

static VALUE
rb_test_image(VALUE self, VALUE data)
{
  VALUE ret;
  struct jpeg_decompress_struct cinfo;
  ext_error_t err_mgr;

  jpeg_create_decompress(&cinfo);

  cinfo.err                   = jpeg_std_error(&err_mgr.jerr);

  err_mgr.jerr.output_message = decode_output_message;
  err_mgr.jerr.emit_message   = decode_emit_message;
  err_mgr.jerr.error_exit     = decode_error_exit;

  cinfo.raw_data_out          = FALSE;
  cinfo.dct_method            = JDCT_FLOAT;

  if (setjmp(err_mgr.jmpbuf)) {
    ret = Qtrue;
  } else {
    ret = Qfalse;

    jpeg_mem_src(&cinfo, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));
    jpeg_read_header(&cinfo, TRUE);
    jpeg_calc_output_dimensions(&cinfo);
  }

  jpeg_destroy_decompress(&cinfo);

  return ret;
}

void
Init_jpeg()
{
  int i;

  module = rb_define_module("JPEG");
  rb_define_singleton_method(module, "broken?", rb_test_image, 1);

  encoder_klass = rb_define_class_under(module, "Encoder", rb_cObject);
  rb_define_alloc_func(encoder_klass, rb_encoder_alloc);
  rb_define_method(encoder_klass, "initialize", rb_encoder_initialize, -1);
  rb_define_method(encoder_klass, "encode", rb_encoder_encode, 1);
  rb_define_alias(encoder_klass, "compress", "encode");
  rb_define_alias(encoder_klass, "<<", "encode");

  encerr_klass  = rb_define_class_under(module,
                                        "EncodeError", rb_eRuntimeError);

  decoder_klass = rb_define_class_under(module, "Decoder", rb_cObject);
  rb_define_alloc_func(decoder_klass, rb_decoder_alloc);
  rb_define_method(decoder_klass, "initialize", rb_decoder_initialize, -1);
  rb_define_method(decoder_klass, "set", rb_decoder_set, 1);
  rb_define_method(decoder_klass, "read_header", rb_decoder_read_header, 1);
  rb_define_method(decoder_klass, "decode", rb_decoder_decode, 1);
  rb_define_alias(decoder_klass, "decompress", "decode");
  rb_define_alias(decoder_klass, "<<", "decode");

  meta_klass    = rb_define_class_under(module, "Meta", rb_cObject);
  rb_define_attr(meta_klass, "width", 1, 0);
  rb_define_attr(meta_klass, "stride", 1, 0);
  rb_define_attr(meta_klass, "height", 1, 0);
  rb_define_attr(meta_klass, "original_colorspace", 1, 0);
  rb_define_attr(meta_klass, "output_colorspace", 1, 0);
  rb_define_attr(meta_klass, "num_components", 1, 0);
  rb_define_attr(meta_klass, "colormap", 1, 0);

  decerr_klass  = rb_define_class_under(module,
                                        "DecodeError", rb_eRuntimeError);

  /*
   * 必要になる都度ID計算をさせるとコストがかかるので、本ライブラリで使用
   * するID値の計算を先に済ませておく
   * 但し、可読性を優先して随時計算する箇所を残しているので注意すること。
   */
  for (i = 0; i < (int)N(encoder_opts_keys); i++) {
      encoder_opts_ids[i] = rb_intern_const(encoder_opts_keys[i]);
  }

  for (i = 0; i < (int)N(decoder_opts_keys); i++) {
      decoder_opts_ids[i] = rb_intern_const(decoder_opts_keys[i]);
  }

  id_meta      = rb_intern_const("@meta");
  id_width     = rb_intern_const("@width");
  id_stride    = rb_intern_const("@stride");
  id_height    = rb_intern_const("@height");
  id_orig_cs   = rb_intern_const("@original_colorspace");
  id_out_cs    = rb_intern_const("@output_colorspace");
  id_ncompo    = rb_intern_const("@num_components");
  id_exif_tags = rb_intern_const("@exif_tags");
  id_colormap  = rb_intern_const("@colormap");
}
