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
#include "ruby/version.h"
#include "ruby/encoding.h"

#define UNIT_LINES                 10

#ifdef DEFAULT_QUALITY
#undef DEFAULT_QUALITY
#endif /* defined(DEFAULT_QUALITY) */

#define DEFAULT_QUALITY            75
#define DEFAULT_INPUT_COLOR_SPACE  JCS_YCbCr
#define DEFAULT_INPUT_COMPONENTS   2
#define DEFAULT_ENCODE_FLAGS       (0)
#define DEFAULT_DECODE_FLAGS       (F_NEED_META)

#define F_NEED_META                0x00000001
#define F_EXPAND_COLORMAP          0x00000002
#define F_PARSE_EXIF               0x00000004
#define F_APPLY_ORIENTATION        0x00000008
#define F_DITHER                   0x00000010
#define F_CREAT                    0x00010000

#define SET_FLAG(ptr, msk)         ((ptr)->flags |= (msk))
#define CLR_FLAG(ptr, msk)         ((ptr)->flags &= ~(msk))
#define TEST_FLAG(ptr, msk)        ((ptr)->flags & (msk))
#define TEST_FLAG_ALL(ptr, msk)    (((ptr)->flags & (msk)) == (msk))

#define SET_DATA(ptr, dat)         ((ptr)->data = (dat))
#define CLR_DATA(ptr)              ((ptr)->data = Qnil)

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
        ((JSAMPARRAY)malloc(sizeof(JSAMPROW) * UNIT_LINES))
#define ALLOC_ROWS(w,c) \
        ((JSAMPROW)malloc(sizeof(JSAMPLE) * (w) * (c) * UNIT_LINES))

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
  "dct_method",               // {str}
  "orientation",              // {integer}
  "stride",                   // {integer}
};

static ID encoder_opts_ids[N(encoder_opts_keys)];

typedef struct {
  struct jpeg_error_mgr jerr;

  char msg[JMSG_LENGTH_MAX+10];
  jmp_buf jmpbuf;
} ext_error_t;

typedef struct {
  int flags;
  int width;
  int stride;
  int height;
  int data_size;

  int format;
  int color_space;
  int components;
  int quality;
  J_DCT_METHOD dct_method;

  struct jpeg_compress_struct cinfo;
  ext_error_t err_mgr;

  JSAMPARRAY array;
  JSAMPROW rows;

  VALUE data;

  struct {
    unsigned char* mem;
    unsigned long size;
  } buf;

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
  "with_exif_tags",           // {bool}
  "orientation"               // {bool}
};

static ID decoder_opts_ids[N(decoder_opts_keys)];

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

  JSAMPARRAY array;

  VALUE data;

  struct {
    int value;
    VALUE buf;
  } orientation;
} jpeg_decode_t;

#if 0
static VALUE
create_runtime_error(const char* fmt, ...)
{
  VALUE ret;
  va_list ap;

  va_start(ap, fmt);
  ret = rb_exc_new_str(rb_eRuntimeError, rb_vsprintf(fmt, ap));
  va_end(ap);

  return ret;
}
#endif

static VALUE
create_argument_error(const char* fmt, ...)
{
  VALUE ret;
  va_list ap;

  va_start(ap, fmt);
  ret = rb_exc_new_str(rb_eArgError, rb_vsprintf(fmt, ap));
  va_end(ap);

  return ret;
}

static VALUE
create_type_error(const char* fmt, ...)
{
  VALUE ret;
  va_list ap;

  va_start(ap, fmt);
  ret = rb_exc_new_str(rb_eTypeError, rb_vsprintf(fmt, ap));
  va_end(ap);

  return ret;
}

static VALUE
create_range_error(const char* fmt, ...)
{
  VALUE ret;
  va_list ap;

  va_start(ap, fmt);
  ret = rb_exc_new_str(rb_eRangeError, rb_vsprintf(fmt, ap));
  va_end(ap);

  return ret;
}

static VALUE
create_not_implement_error(const char* fmt, ...)
{
  VALUE ret;
  va_list ap;

  va_start(ap, fmt);
  ret = rb_exc_new_str(rb_eNotImpError, rb_vsprintf(fmt, ap));
  va_end(ap);

  return ret;
}

static VALUE
create_memory_error()
{
  return rb_exc_new_str(rb_eRangeError, rb_str_new_cstr("no memory"));
}

static void
output_message(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;

  (*err->jerr.format_message)(cinfo, err->msg);
}

static void
emit_message(j_common_ptr cinfo, int msg_level)
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
error_exit(j_common_ptr cinfo)
{
  ext_error_t* err;

  err = (ext_error_t*)cinfo->err;
  (*err->jerr.format_message)(cinfo, err->msg);
  longjmp(err->jmpbuf, 1);
}

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
rb_encoder_mark(void* _ptr)
{
  jpeg_encode_t* ptr;

  ptr = (jpeg_encode_t*)_ptr;

  if (ptr->data != Qnil) {
    rb_gc_mark(ptr->data);
  }
}

static void
rb_encoder_free(void* _ptr)
{
  jpeg_encode_t* ptr;

  ptr = (jpeg_encode_t*)_ptr;

  if (ptr->array != NULL) {
    free(ptr->array);
  }

  if (ptr->rows != NULL) {
    free(ptr->rows);
  }

  if (ptr->buf.mem != NULL) {
    free(ptr->buf.mem);
  }

  if (TEST_FLAG(ptr, F_CREAT)) {
    jpeg_destroy_compress(&ptr->cinfo);
  }

  free(ptr);
}

static size_t
rb_encoder_size(const void* _ptr)
{
  size_t ret;
  jpeg_encode_t* ptr;

  ptr  = (jpeg_encode_t*)_ptr;

  ret  = sizeof(jpeg_encode_t);
  ret += sizeof(JSAMPROW) * UNIT_LINES; 
  ret += sizeof(JSAMPLE) * ptr->stride * UNIT_LINES;

  return ret;
}

#if RUBY_API_VERSION_CODE > 20600
static const rb_data_type_t jpeg_encoder_data_type = {
  "libjpeg-ruby encoder object",     // wrap_struct_name
  {
    rb_encoder_mark,                 // function.dmark
    rb_encoder_free,                 // function.dfree
    rb_encoder_size,                 // function.dsize
    NULL,                            // function.dcompact
    {NULL},                          // function.reserved
  },                                
  NULL,                              // parent
  NULL,                              // data
  (VALUE)RUBY_TYPED_FREE_IMMEDIATELY // flags
};
#else /* RUBY_API_VERSION_CODE > 20600 */
static const rb_data_type_t jpeg_encoder_data_type = {
  "libjpeg-ruby encoder object",     // wrap_struct_name
  {
    rb_encoder_mark,                 // function.dmark
    rb_encoder_free,                 // function.dfree
    rb_encoder_size,                 // function.dsize
    {NULL, NULL},                    // function.reserved
  },                                
  NULL,                              // parent
  NULL,                              // data
  (VALUE)RUBY_TYPED_FREE_IMMEDIATELY // flags
};
#endif /* RUBY_API_VERSION_CODE > 20600 */

static VALUE
rb_encoder_alloc(VALUE self)
{
  jpeg_encode_t* ptr;

  ptr = ALLOC(jpeg_encode_t);
  memset(ptr, 0, sizeof(*ptr));

  ptr->flags = DEFAULT_ENCODE_FLAGS;

  return TypedData_Wrap_Struct(encoder_klass, &jpeg_encoder_data_type, ptr);
}

static VALUE
eval_encoder_pixel_format_opt(jpeg_encode_t* ptr, VALUE opt)
{
  VALUE ret;
  int format;
  int color_space;
  int components;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    format      = FMT_YUV422;
    color_space = JCS_YCbCr;
    components  = 3;
    break;

  case T_STRING:
  case T_SYMBOL:
    if (EQ_STR(opt, "YUV422") || EQ_STR(opt, "YUYV")) {
      format      = FMT_YUV422;
      color_space = JCS_YCbCr;
      components  = 3;

    } else if (EQ_STR(opt, "RGB565")) {
      format      = FMT_RGB565;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "RGB") || EQ_STR(opt, "RGB24")) {
      format      = FMT_RGB;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "BGR") || EQ_STR(opt, "BGR24")) {
      format      = FMT_BGR;
      color_space = JCS_EXT_BGR;
      components  = 3;

    } else if (EQ_STR(opt, "YUV444") || EQ_STR(opt, "YCbCr")) {
      format      = FMT_YUV;
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

    } else if (EQ_STR(opt, "GRAYSCALE")) {
      format      = FMT_GRAYSCALE;
      color_space = JCS_GRAYSCALE;
      components  = 1;

    } else {
      ret = create_argument_error("unsupportd :pixel_format option value");
    }
    break;

  default:
    ret = create_type_error("unsupportd :pixel_format option type");
    break;
  }

  if (!RTEST(ret)) {
    ptr->format      = format;
    ptr->color_space = color_space;
    ptr->components  = components;
  }

  return ret;
}

static VALUE
eval_encoder_quality_opt(jpeg_encode_t* ptr, VALUE opt)
{
  VALUE ret;
  long quality;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    quality = DEFAULT_QUALITY;
    break;

  case T_FLOAT:
    if (isnan(NUM2DBL(opt)) || isinf(NUM2DBL(opt))) {
      ret = create_argument_error("unsupportd :quality option value");

    } else if (NUM2DBL(opt) < 0.0) {
      ret = create_range_error(":quality less than 0");

    } else if (NUM2DBL(opt) > 100.0) {
      ret = create_range_error(":quality greater than 100");

    } else {
      quality = NUM2INT(opt);
    }
    break;

  case T_FIXNUM:
    if (FIX2LONG(opt) < 0) {
      ret = create_range_error(":quality less than 0");

    } if (FIX2LONG(opt) > 100) {
      ret = create_range_error(":quality greater than 100");

    } else {
      quality = FIX2INT(opt);
    }
    break;

  default:
    ret = create_type_error("unsupportd :quality option type");
    break;
  }

  if (!RTEST(ret)) ptr->quality = quality;

  return ret;
}

static VALUE
eval_encoder_dct_method_opt(jpeg_encode_t* ptr, VALUE opt)
{
  VALUE ret;
  int dct_method;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    dct_method = JDCT_FASTEST;
    break;

  case T_STRING:
  case T_SYMBOL:
    if (EQ_STR(opt, "FASTEST")) {
      dct_method = JDCT_FASTEST;

    } else if (EQ_STR(opt, "ISLOW")) {
      dct_method = JDCT_ISLOW;

    } else if (EQ_STR(opt, "IFAST")) {
      dct_method = JDCT_IFAST;

    } else if (EQ_STR(opt, "FLOAT")) {
      dct_method = JDCT_FLOAT;

    } else {
      ret = create_argument_error("unsupportd :dct_method option value");
    }
    break;

  default:
    ret = create_type_error("unsupportd :dct_method option type");
    break;
  }

  if (!RTEST(ret)) ptr->dct_method = dct_method;

  return ret;
}
 
static VALUE
eval_encoder_orientation_opt(jpeg_encode_t* ptr, VALUE opt)
{
  VALUE ret;
  int orientation;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    orientation = 0;
    break;

  case T_FIXNUM:
    orientation = FIX2INT(opt);
    if (orientation < 1 || orientation > 8) {
      ret = create_range_error(":orientation option ouf range");
    } 
    break;

  default:
    ret = create_type_error("Unsupportd :orientation option type.");
    break;
  }

  if (!RTEST(ret)) ptr->orientation = orientation;

  return ret;
}

static VALUE
eval_encoder_stride_opt(jpeg_encode_t* ptr, VALUE opt)
{
  VALUE ret;
  int stride;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    stride = ptr->width * ptr->components;
    break;

  case T_FIXNUM:
    stride = FIX2INT(opt);
    if (stride < (ptr->width * ptr->components)) {
      ret = create_range_error(":stride too little");
    }
    break;

  default:
    ret = create_type_error("unsupportd :stride option type");
  }

  if (!RTEST(ret)) ptr->stride = stride;

  return ret;
}

static VALUE
set_encoder_context(jpeg_encode_t* ptr, int wd, int ht, VALUE opt)
{
  VALUE ret;
  VALUE opts[N(encoder_opts_ids)];
  JSAMPARRAY ary;
  JSAMPROW rows;

  int i;

  /*
   * initialize
   */
  ret  = Qnil;
  ary  = NULL;
  rows = NULL;

  /*
   * argument check
   */
  do {
    if (wd <= 0) {
      ret = create_range_error("image width less equal zero");
      break;
    }

    if (ht <= 0) {
      ret = create_range_error("image height less equal zero");
      break;
    }
  } while (0);

  /*
   * parse options
   */
  if (!RTEST(ret)) do {
    rb_get_kwargs(opt, encoder_opts_ids, 0, N(encoder_opts_ids), opts);

    // オプション評価で使用するので前もって設定しておく
    ptr->width  = wd;
    ptr->height = ht;

    ret = eval_encoder_pixel_format_opt(ptr, opts[0]);
    if (RTEST(ret)) break;

    ret = eval_encoder_quality_opt(ptr, opts[1]);
    if (RTEST(ret)) break;

    ret = eval_encoder_dct_method_opt(ptr, opts[2]);
    if (RTEST(ret)) break;

    ret = eval_encoder_orientation_opt(ptr, opts[3]);
    if (RTEST(ret)) break;

    ret = eval_encoder_stride_opt(ptr, opts[4]);
    if (RTEST(ret)) break;
  } while (0);

  /*
   * alloc memory
   */
  if (!RTEST(ret)) do {
    ary = ALLOC_ARRAY();
    if (ary == NULL) {
      ret = create_memory_error();
      break;
    }

    rows = ALLOC_ROWS(ptr->width, ptr->components);
    if (rows == NULL) {
      ret = create_memory_error();
      break;
    }
  } while (0);

  /*
   * set the rest context parameter
   */
  if (!RTEST(ret)) {
    ptr->err_mgr.jerr.output_message = output_message;
    ptr->err_mgr.jerr.emit_message   = emit_message;
    ptr->err_mgr.jerr.error_exit     = error_exit;

    ptr->data_size = ptr->stride * ptr->height;
    ptr->buf.mem   = NULL;
    ptr->buf.size  = 0;
    ptr->array     = ary;
    ptr->rows      = rows;
    ptr->data      = Qnil;

    for (i = 0; i < UNIT_LINES; i++) {
      ptr->array[i] = ptr->rows + (i * ptr->width * ptr->components);
    }
  }

  /*
   * setup libjpeg
   */
  if (!RTEST(ret)) {
    jpeg_create_compress(&ptr->cinfo);
    SET_FLAG(ptr, F_CREAT);

    ptr->cinfo.err              = jpeg_std_error(&ptr->err_mgr.jerr);
    ptr->cinfo.image_width      = ptr->width;
    ptr->cinfo.image_height     = ptr->height;
    ptr->cinfo.in_color_space   = ptr->color_space;
    ptr->cinfo.input_components = ptr->components;

    ptr->cinfo.optimize_coding  = TRUE;
    ptr->cinfo.arith_code       = TRUE;
    ptr->cinfo.raw_data_in      = FALSE;
    ptr->cinfo.dct_method       = ptr->dct_method;

    jpeg_set_defaults(&ptr->cinfo);
    jpeg_set_quality(&ptr->cinfo, ptr->quality, TRUE);
    jpeg_suppress_tables(&ptr->cinfo, TRUE);
  }

  /*
   * post process
   */
  if (RTEST(ret)) {
    if (ary != NULL) free(ary);
    if (rows != NULL) free(rows);
  }

  return ret;
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
  VALUE exc;
  VALUE wd;
  VALUE ht;
  VALUE opt;

  /*
   * initialize
   */
  exc = Qnil;

  TypedData_Get_Struct(self, jpeg_encode_t, &jpeg_encoder_data_type, ptr);

  /*
   * parse arguments
   */
  rb_scan_args(argc, argv, "2:", &wd, &ht, &opt);

  /*
   * argument check
   */
  do {
    if (TYPE(wd) != T_FIXNUM) {
      exc = create_argument_error("invalid width");
      break;
    }

    if (TYPE(ht) != T_FIXNUM) {
      exc = create_argument_error("invalid height");
      break;
    }
  } while (0);

  /*
   * set context
   */ 
  if (!RTEST(exc)) {
    exc = set_encoder_context(ptr, FIX2INT(wd), FIX2INT(ht), opt);
  }

  /*
   * post process
   */
  if (RTEST(exc)) rb_exc_raise(exc);

  return Qtrue;
}

static void
push_rows_yuv422(JSAMPROW dst, int wd, int st, uint8_t* data, int nrow)
{
  uint8_t* src;
  int i;
  int j;

  for (i = 0; i < nrow; i++) {
    src = data;

    for (j = 0; j < wd; j += 2) {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[3];
      dst[3] = src[2];
      dst[4] = src[1];
      dst[5] = src[3];

      dst += 6;
      src += 4;
    } 

    data += st;
  }
}

static void
push_rows_rgb565(JSAMPROW dst, int wd, int st, uint8_t* data, int nrow)
{
  uint8_t* src;
  int i;
  int j;

  for (i = 0; i < nrow; i++) {
    src = data;

    for (j = 0; j < wd; j++) {
      dst[0] = src[1] & 0xf8;
      dst[1] = ((src[1] << 5) & 0xe0) | ((src[0] >> 3) & 0x1c);
      dst[2] = (src[0] << 3) & 0xf8;

      dst += 3;
      src += 2;
    }

    data += st;
  }
}

static void
push_rows_comp3(JSAMPROW rows, int wd, int st, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * 3;

  for (i = 0; i < nrow; i++) {
    memcpy(rows, data, size);

    rows += size;
    data += st;
  }
}

static void
push_rows_comp4(JSAMPROW rows, int wd, int st, uint8_t* data, int nrow)
{
  int size;
  int i;

  size = wd * 4;

  for (i = 0; i < nrow; i++) {
    memcpy(rows, data, size);

    rows += size;
    data += st;
  }
}

static void
push_rows_grayscale(JSAMPROW rows, int wd, int st, uint8_t* data, int nrow)
{
  int i;

  for (i = 0; i < nrow; i++) {
    memcpy(rows, data, wd);

    rows += wd;
    data += st;
  }
}

static void
push_rows(jpeg_encode_t* ptr, uint8_t* data, int nrow)
{
  switch (ptr->format) {
  case FMT_YUV422:
    push_rows_yuv422(ptr->rows, ptr->width, ptr->stride, data, nrow);
    break;

  case FMT_RGB565:
    push_rows_rgb565(ptr->rows, ptr->width, ptr->stride, data, nrow);
    break;

  case FMT_YUV:
  case FMT_RGB:
  case FMT_BGR:
    push_rows_comp3(ptr->rows, ptr->width, ptr->stride, data, nrow);
    break;

  case FMT_RGB32:
  case FMT_BGR32:
    push_rows_comp4(ptr->rows, ptr->width, ptr->stride, data, nrow);
    break;

  case FMT_GRAYSCALE:
    push_rows_grayscale(ptr->rows, ptr->width, ptr->stride, data, nrow);
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
do_encode(VALUE _ptr)
{
  VALUE ret;
  jpeg_encode_t* ptr;
  uint8_t* data;
  int nrow;

  /*
   * initialize
   */
  ret  = Qnil;
  ptr  = (jpeg_encode_t*)_ptr;
  data = (uint8_t*)RSTRING_PTR(ptr->data);

  /*
   * do encode
   */
  if (setjmp(ptr->err_mgr.jmpbuf)) {
    /*
     * when error occurred
     */
    jpeg_abort_compress(&ptr->cinfo);
    rb_raise(encerr_klass, "%s", ptr->err_mgr.msg);

  } else {
    /*
     * normal path
     */
    jpeg_start_compress(&ptr->cinfo, TRUE);

    if (ptr->orientation != 0) {
      put_exif_tags(ptr);
    }

    while (ptr->cinfo.next_scanline < ptr->cinfo.image_height) {
      nrow = ptr->cinfo.image_height - ptr->cinfo.next_scanline;
      if (nrow > UNIT_LINES) nrow = UNIT_LINES;

      push_rows(ptr, data, nrow);

      jpeg_write_scanlines(&ptr->cinfo, ptr->array, nrow);
      data += (ptr->stride * nrow);
    }

    jpeg_finish_compress(&ptr->cinfo);

    /*
     * build return data
     */
    ret = rb_str_buf_new(ptr->buf.size);
    rb_str_set_len(ret, ptr->buf.size);

    memcpy(RSTRING_PTR(ret), ptr->buf.mem, ptr->buf.size);
  }

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
  int state;
  jpeg_encode_t* ptr;

  /*
   * initialize
   */
  ret   = Qnil;
  state = 0;

  TypedData_Get_Struct(self, jpeg_encode_t, &jpeg_encoder_data_type, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  if (RSTRING_LEN(data) < ptr->data_size) {
    ARGUMENT_ERROR("image data is too short");
  }

  if (RSTRING_LEN(data) > ptr->data_size) {
    ARGUMENT_ERROR("image data is too large");
  }

  /*
   * alloc memory
   */
  jpeg_mem_dest(&ptr->cinfo, &ptr->buf.mem, &ptr->buf.size); 
  if (ptr->buf.mem == NULL) {
    RUNTIME_ERROR("jpeg_mem_dest() failed");
  }

  /*
   * prepare
   */
  SET_DATA(ptr, data);

  /*
   * do encode
   */
  ret = rb_protect(do_encode, (VALUE)ptr, &state);

  /*
   * post process
   */
  CLR_DATA(ptr);

  if (ptr->buf.mem != NULL) {
    free(ptr->buf.mem);

    ptr->buf.mem  = NULL;
    ptr->buf.size = 0;
  }

  if (state != 0) rb_jump_tag(state);

  return ret;
}

static void
rb_decoder_mark(void* _ptr)
{
  jpeg_decode_t* ptr;

  ptr = (jpeg_decode_t*)_ptr; 

  if (ptr->orientation.buf != Qnil) {
    rb_gc_mark(ptr->orientation.buf);
  }

  if (ptr->data != Qnil) {
    rb_gc_mark(ptr->data);
  }
}

static void
rb_decoder_free(void* _ptr)
{
  jpeg_decode_t* ptr;

  ptr = (jpeg_decode_t*)_ptr;

  if (ptr->array != NULL) {
    free(ptr->array);
  }

  ptr->orientation.buf = Qnil;
  ptr->data            = Qnil;

  if (TEST_FLAG(ptr, F_CREAT)) {
    jpeg_destroy_decompress(&ptr->cinfo);
  }

  free(ptr);
}

static size_t
rb_decoder_size(const void* ptr)
{
  size_t ret;

  ret  = sizeof(jpeg_decode_t);

  return ret;
}

#if RUBY_API_VERSION_CODE > 20600
static const rb_data_type_t jpeg_decoder_data_type = {
  "libjpeg-ruby decoder object",     // wrap_struct_name
  {                                 
    rb_decoder_mark,                 // function.dmark
    rb_decoder_free,                 // function.dfree
    rb_decoder_size,                 // function.dsize
    NULL,                            // function.dcompact
    {NULL},                          // function.reserved
  },                                
  NULL,                              // parent
  NULL,                              // data
  (VALUE)RUBY_TYPED_FREE_IMMEDIATELY // flags
};
#else /* RUBY_API_VERSION_CODE > 20600 */
static const rb_data_type_t jpeg_decoder_data_type = {
  "libjpeg-ruby decoder object",     // wrap_struct_name
  {                                 
    rb_decoder_mark,                 // function.dmark
    rb_decoder_free,                 // function.dfree
    rb_decoder_size,                 // function.dsize
    {NULL, NULL},                    // function.reserved
  },                                
  NULL,                              // parent
  NULL,                              // data
  (VALUE)RUBY_TYPED_FREE_IMMEDIATELY // flags
};
#endif /* RUBY_API_VERSION_CODE > 20600 */

static VALUE
rb_decoder_alloc(VALUE self)
{
  jpeg_decode_t* ptr;

  ptr = ALLOC(jpeg_decode_t);
  memset(ptr, 0, sizeof(*ptr));

  ptr->flags = DEFAULT_DECODE_FLAGS;

  return TypedData_Wrap_Struct(decoder_klass, &jpeg_decoder_data_type, ptr);
}

static VALUE
eval_decoder_pixel_format_opt(jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  int format;
  int color_space;
  int components;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    format      = FMT_RGB;
    color_space = JCS_RGB;
    components  = 3;
    break;

  case T_STRING:
  case T_SYMBOL:
    if(EQ_STR(opt, "RGB") || EQ_STR(opt, "RGB24")) {
      format      = FMT_RGB;
      color_space = JCS_RGB;
      components  = 3;

    } else if (EQ_STR(opt, "YUV422") || EQ_STR(opt, "YUYV")) {
      ret = create_not_implement_error( "not implemented colorspace");

    } else if (EQ_STR(opt, "RGB565")) {
      ret = create_not_implement_error( "not implemented colorspace");

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
      ret = create_argument_error("unsupportd :pixel_format option value");
    }
    break;

  default:
    ret = create_type_error("unsupportd :pixel_format option type");
    break;
  }

  if (!RTEST(ret)) {
    ptr->format               = format;
    ptr->out_color_space      = color_space;
    ptr->out_color_components = components;
  }

  return ret;
}

static VALUE
eval_decoder_output_gamma_opt(jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  double gamma;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    gamma = 0.0;
    break;

  case T_FIXNUM:
  case T_FLOAT:
    if (isnan(NUM2DBL(opt)) || isinf(NUM2DBL(opt))) {
      ret = create_argument_error("unsupported :output_gamma value");
    } else {
      gamma = NUM2DBL(opt);
    }
    break;

  default:
    ret = create_type_error("unsupported :output_gamma type");
    break;
  }

  if (!RTEST(ret)) ptr->output_gamma = gamma;

  return ret;
}

static VALUE
eval_decoder_do_fancy_upsampling_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    ptr->do_fancy_upsampling = TRUE;
  } else {
    ptr->do_fancy_upsampling = FALSE;
  }

  return Qnil;
}

static VALUE
eval_decoder_do_smoothing_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    ptr->do_block_smoothing = TRUE;
  } else {
    ptr->do_block_smoothing = FALSE;
  }

  return Qnil;
}

static VALUE
eval_decoder_dither_opt( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  VALUE tmp;
  int mode;
  int quant;
  int pass2;
  int ncol;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    mode  = JDITHER_NONE;
    quant = FALSE;
    ncol  = 0;
    pass2 = FALSE;
    break;

  case T_ARRAY:
    if (RARRAY_LEN(opt) != 3) {
      ret = create_argument_error(":dither invalid size");

    } else do {
      /*
       * dither mode
       */
      tmp = rb_ary_entry(opt, 0);
      if (TYPE(tmp) != T_STRING && TYPE(tmp) != T_SYMBOL) {
        ret = create_type_error("unsupported dither mode type");
        break;

      } else if (EQ_STR(tmp, "NONE")) {
        mode  = JDITHER_NONE;
        quant = FALSE;

      } else if(EQ_STR(tmp, "ORDERED")) {
        mode  = JDITHER_ORDERED;
        quant = TRUE;

      } else if(EQ_STR(tmp, "FS")) {
        mode  = JDITHER_FS;
        quant = TRUE;

      } else {
        ret = create_argument_error("dither mode is illeagal value.");
        break;
      }

      /*
       * 2 pass flag
       */
      pass2 = (RTEST(rb_ary_entry(opt, 1)))? TRUE: FALSE;

      /*
       * number of color
       */
      tmp = rb_ary_entry(opt, 2);
      if (TYPE(tmp) != T_FIXNUM) {
        ret = create_type_error("unsupported number of colors type");
        break;

      } else if (FIX2LONG(tmp) < 8) {
        ret = create_range_error("number of colors less than 0");
        break;

      } else if (FIX2LONG(tmp) > 256) {
        ret = create_range_error("number of colors greater than 256");
        break;

      } else {
        ncol = FIX2INT(tmp); 
      }
    } while (0);
    break;

  default:
    ret = create_type_error("unsupported :dither type");
    break;
  }

  if (!RTEST(ret)) {
    ptr->dither_mode              = mode;
    ptr->quantize_colors          = quant;
    ptr->two_pass_quantize        = pass2;
    ptr->desired_number_of_colors = ncol;

    if (mode != JDITHER_NONE) SET_FLAG(ptr, F_DITHER);
  }

  return ret;
}

#if 0
static void
eval_decoder_use_1pass_quantizer_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    ptr->enable_1pass_quant = TRUE;
  } else {
    ptr->enable_1pass_quant = FALSE;
  }

  return Qnil;
}

static VALUE
eval_decoder_use_external_colormap_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
      ptr->enable_external_quant = TRUE;
    } else {
      ptr->enable_external_quant = FALSE;
    }
  }

  return Qnil;
}

static VALUE
eval_decoder_use_2pass_quantizer_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
      ptr->enable_2pass_quant = TRUE;
    } else {
      ptr->enable_2pass_quant = FALSE;
    }
  }

  return Qnil;
}
#endif

static VALUE
eval_decoder_without_meta_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    CLR_FLAG(ptr, F_NEED_META);
  } else {
    SET_FLAG(ptr, F_NEED_META);
  }

  return Qnil;
}

static VALUE
eval_decoder_expand_colormap_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    SET_FLAG(ptr, F_EXPAND_COLORMAP);
  } else {
    CLR_FLAG(ptr, F_EXPAND_COLORMAP);
  }

  return Qnil;
}


static VALUE
eval_decoder_scale_opt(jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  int scale_num;
  int scale_denom;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    scale_num   = 1;
    scale_denom = 1;
    break;

  case T_FIXNUM:
    if (FIX2LONG(opt) <= 0) {
      ret = create_range_error(":scale less equal 0");

    } else {
      scale_num   = FIX2INT(opt) * 1000;
      scale_denom = 1000;
    }
    break;

  case T_FLOAT:
    if (isnan(NUM2DBL(opt)) || isinf(NUM2DBL(opt))) {
      ret = create_argument_error("unsupportd :quality option value");
      
    } else if (NUM2DBL(opt) <= 0.0) {
      ret = create_range_error(":scale less equal 0");

    } else {
      scale_num   = (int)(NUM2DBL(opt) * 1000.0);
      scale_denom = 1000;
    }
    break;

  case T_RATIONAL:
    scale_num   = (int)FIX2LONG(rb_rational_num(opt));
    scale_denom = (int)FIX2LONG(rb_rational_den(opt));
    break;

  default:
    ret = create_type_error("unsupportd :scale option type");
    break;
  }

  if (!RTEST(ret)) {
    ptr->scale_num   = scale_num;
    ptr->scale_denom = scale_denom;
  }

  return ret;
}

static VALUE
eval_decoder_dct_method_opt(jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  int dct_method;

  ret = Qnil;

  switch (TYPE(opt)) {
  case T_UNDEF:
    dct_method = JDCT_FASTEST;
    break;

  case T_STRING:
  case T_SYMBOL:
    if (EQ_STR(opt, "FASTEST")) {
      dct_method = JDCT_FASTEST;

    } else if (EQ_STR(opt, "ISLOW")) {
      dct_method = JDCT_ISLOW;

    } else if (EQ_STR(opt, "IFAST")) {
      dct_method = JDCT_IFAST;

    } else if (EQ_STR(opt, "FLOAT")) {
      dct_method = JDCT_FLOAT;

    } else {
      ret = create_argument_error("unsupportd :dct_method option value");
    }
    break;

  default:
    ret = create_type_error("unsupportd :dct_method option type");
    break;
  }

  if (!RTEST(ret)) ptr->dct_method = dct_method;

  return ret;
}

static VALUE
eval_decoder_with_exif_tags_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    SET_FLAG(ptr, F_PARSE_EXIF);
  } else {
    CLR_FLAG(ptr, F_PARSE_EXIF);
  }

  return Qnil;
}

static VALUE
eval_decoder_orientation_opt(jpeg_decode_t* ptr, VALUE opt)
{
  if (opt != Qundef && RTEST(opt)) {
    SET_FLAG(ptr, F_APPLY_ORIENTATION);
  } else {
    CLR_FLAG(ptr, F_APPLY_ORIENTATION);
  }

  return Qnil;
}

static VALUE
set_decoder_context( jpeg_decode_t* ptr, VALUE opt)
{
  VALUE ret;
  VALUE opts[N(decoder_opts_ids)];
  JSAMPARRAY ary;

  /*
   * initialize
   */
  ret = Qnil;
  ary = NULL;

  /*
   * parse options
   */
  do {
    rb_get_kwargs(opt, decoder_opts_ids, 0, N(decoder_opts_ids), opts);

    /*
     * set context
     */
    ret = eval_decoder_pixel_format_opt(ptr, opts[0]);
    if (RTEST(ret)) break;

    ret = eval_decoder_output_gamma_opt(ptr, opts[1]);
    if (RTEST(ret)) break;

    ret = eval_decoder_do_fancy_upsampling_opt(ptr, opts[2]);
    if (RTEST(ret)) break;

    ret = eval_decoder_do_smoothing_opt(ptr, opts[3]);
    if (RTEST(ret)) break;

    ret = eval_decoder_dither_opt(ptr, opts[4]);
    if (RTEST(ret)) break;

#if 0
    ret = eval_decoder_use_1pass_quantizer_opt(ptr, opts[5]);
    if (RTEST(ret)) break;

    ret = eval_decoder_use_external_colormap_opt(ptr, opts[6]);
    if (RTEST(ret)) break;

    ret = eval_decoder_use_2pass_quantizer_opt(ptr, opts[7]);
    if (RTEST(ret)) break;
#endif

    ret = eval_decoder_without_meta_opt(ptr, opts[5]);
    if (RTEST(ret)) break;

    ret = eval_decoder_expand_colormap_opt(ptr, opts[6]);
    if (RTEST(ret)) break;

    ret = eval_decoder_scale_opt(ptr, opts[7]);
    if (RTEST(ret)) break;

    ret = eval_decoder_dct_method_opt(ptr, opts[8]);
    if (RTEST(ret)) break;

    ret = eval_decoder_with_exif_tags_opt(ptr, opts[9]);
    if (RTEST(ret)) break;

    ret = eval_decoder_orientation_opt(ptr, opts[10]);
    if (RTEST(ret)) break;
  } while (0);

  /*
   * alloc memory
   */
  if (!RTEST(ret)) {
    ary = ALLOC_ARRAY();
    if (ary == NULL) ret = create_memory_error();
  }

  /*
   * set the rest context parameter
   */
  if (!RTEST(ret)) {
    ptr->err_mgr.jerr.output_message = output_message;
    ptr->err_mgr.jerr.emit_message   = emit_message;
    ptr->err_mgr.jerr.error_exit     = error_exit;

    // 現時点でオプションでの対応をおこなっていないので
    // ここで値を設定
    ptr->enable_1pass_quant    = FALSE;
    ptr->enable_external_quant = FALSE;
    ptr->enable_2pass_quant    = FALSE;
    ptr->buffered_image        = FALSE;

#if 0
    if (ptr->enable_1pass_quant == TRUE ||
        ptr->enable_external_quant == TRUE ||
        ptr->enable_2pass_quant == TRUE) {
      ptr->buffered_image = TRUE;

    } else {
      ptr->buffered_image = FALSE;
    }
#endif

    ptr->array             = ary;
    ptr->data              = Qnil;
    ptr->orientation.value = 0;
    ptr->orientation.buf   = Qnil;
  }

  /*
   * setup libjpeg
   */
  if (!RTEST(ret)) {
    jpeg_create_decompress(&ptr->cinfo);
    SET_FLAG(ptr, F_CREAT);

    ptr->cinfo.err = jpeg_std_error(&ptr->err_mgr.jerr);
  }

  return ret;
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
  VALUE exc;
  VALUE opt;

  /*
   * initialize
   */
  TypedData_Get_Struct(self, jpeg_decode_t, &jpeg_decoder_data_type, ptr);

  /*
   * parse arguments
   */
  rb_scan_args( argc, argv, "0:", &opt);

  /*
   * set context
   */ 
  exc = set_decoder_context(ptr, opt);

  /*
   * post process
   */
  if (RTEST(exc)) rb_exc_raise(exc);

  return Qtrue;
}

static VALUE
rb_decoder_set(VALUE self, VALUE opt)
{
  jpeg_decode_t* ptr;
  VALUE exc;

  /*
   * initialize
   */
  TypedData_Get_Struct(self, jpeg_decode_t, &jpeg_decoder_data_type, ptr);

  /*
   * check argument
   */
  Check_Type(opt, T_HASH);

  /*
   * set context
   */ 
  exc = set_decoder_context(ptr, opt);

  /*
   * post process
   */
  if (RTEST(exc)) rb_exc_raise(exc);

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
do_read_header(VALUE _ptr)
{
  VALUE ret;
  jpeg_decode_t* ptr;
  uint8_t* data;
  size_t size;

  /*
   * initialize
   */
  ret  = Qnil;
  ptr  = (jpeg_decode_t*)_ptr;
  data = (uint8_t*)RSTRING_PTR(ptr->data);
  size = RSTRING_LEN(ptr->data);

  /*
   * process body
   */
  ptr->cinfo.raw_data_out = FALSE;
  ptr->cinfo.dct_method   = JDCT_FLOAT;

  if (setjmp(ptr->err_mgr.jmpbuf)) {
    /*
     * when error occurred
     */
    rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);

  } else {
    /*
     * normal path
     */
    jpeg_mem_src(&ptr->cinfo, data, size);

    if (TEST_FLAG(ptr, F_PARSE_EXIF | F_APPLY_ORIENTATION)) {
      jpeg_save_markers(&ptr->cinfo, JPEG_APP1, 0xFFFF);
    }

    jpeg_read_header(&ptr->cinfo, TRUE);
    jpeg_calc_output_dimensions(&ptr->cinfo);

    if (TEST_FLAG(ptr, F_APPLY_ORIENTATION)) {
      pick_exif_orientation(ptr);
    }

    ret = create_meta(ptr);
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
  int state;

  /*
   * initialize
   */
  ret   = Qnil;
  state = 0;

  TypedData_Get_Struct(self, jpeg_decode_t, &jpeg_decoder_data_type, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  /*
   * prepare
   */
  SET_DATA(ptr, data);

  /*
   * do encode
   */
  ret = rb_protect(do_read_header, (VALUE)ptr, &state);

  /*
   * post process
   */
  CLR_DATA(ptr);

  if (state != 0) rb_jump_tag(state);

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
do_decode(VALUE _ptr)
{
  VALUE ret;

  jpeg_decode_t* ptr;
  uint8_t* data;
  size_t size;

  struct jpeg_decompress_struct* cinfo;
  JSAMPARRAY array;

  size_t stride;
  size_t raw_sz;
  uint8_t* raw;
  int i;
  int j;

  /*
   * initialize
   */
  ret   = Qnil; // warning対策
  ptr   = (jpeg_decode_t*)_ptr;
  data  = (uint8_t*)RSTRING_PTR(ptr->data);
  size  = RSTRING_LEN(ptr->data);
  cinfo = &ptr->cinfo;
  array = ptr->array;

  /*
   * do decode
   */
  if (setjmp(ptr->err_mgr.jmpbuf)) {
    /*
     * when error occurred
     */
    jpeg_abort_decompress(&ptr->cinfo);
    rb_raise(decerr_klass, "%s", ptr->err_mgr.msg);

  } else {
    /*
     * initialize
     */
    jpeg_mem_src(cinfo, data, size);

    if (TEST_FLAG(ptr, F_PARSE_EXIF | F_APPLY_ORIENTATION)) {
      jpeg_save_markers(&ptr->cinfo, JPEG_APP1, 0xFFFF);
    }

    jpeg_read_header(cinfo, TRUE);
    jpeg_calc_output_dimensions(cinfo);

    /*
     * configuration
     */
    cinfo->raw_data_out             = FALSE;
    cinfo->dct_method               = ptr->dct_method;
           
    cinfo->out_color_space          = ptr->out_color_space;
    cinfo->out_color_components     = ptr->out_color_components;
    cinfo->scale_num                = ptr->scale_num;
    cinfo->scale_denom              = ptr->scale_denom;
    cinfo->output_gamma             = ptr->output_gamma;
    cinfo->do_fancy_upsampling      = ptr->do_fancy_upsampling;
    cinfo->do_block_smoothing       = ptr->do_block_smoothing;
    cinfo->quantize_colors          = ptr->quantize_colors;
    cinfo->dither_mode              = ptr->dither_mode;
    cinfo->two_pass_quantize        = ptr->two_pass_quantize;
    cinfo->desired_number_of_colors = ptr->desired_number_of_colors;
    cinfo->enable_1pass_quant       = ptr->enable_1pass_quant;
    cinfo->enable_external_quant    = ptr->enable_external_quant;
    cinfo->enable_2pass_quant       = ptr->enable_2pass_quant;

    /*
     * decode process
     */
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

    jpeg_finish_decompress(&ptr->cinfo);

    /*
     * build return data
     */
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
  }

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
  int state;

  /*
   * initialize
   */
  ret   = Qnil;
  state = 0;

  TypedData_Get_Struct(self, jpeg_decode_t, &jpeg_decoder_data_type, ptr);

  /*
   * argument check
   */
  Check_Type(data, T_STRING);

  /*
   * prepare
   */
  SET_DATA(ptr, data);

  /*
   * do decode
   */
  ret = rb_protect(do_decode, (VALUE)ptr, &state);

  /*
   * post process
   */
  CLR_DATA(ptr);

  if (state != 0) rb_jump_tag(state);

  return ret;
}

static VALUE
rb_test_image(VALUE self, VALUE data)
{
  VALUE ret;
  struct jpeg_decompress_struct cinfo;
  ext_error_t err_mgr;

  cinfo.raw_data_out = FALSE;
  cinfo.dct_method   = JDCT_FLOAT;

  if (setjmp(err_mgr.jmpbuf)) {
    ret = Qfalse;

  } else {
    jpeg_mem_src(&cinfo, (uint8_t*)RSTRING_PTR(data), RSTRING_LEN(data));
    jpeg_read_header(&cinfo, TRUE);
    jpeg_calc_output_dimensions(&cinfo);

    ret = Qtrue;
  }

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
