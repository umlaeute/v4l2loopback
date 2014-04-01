	/* here come the packed formats */
		.name     = "32 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.depth    = 32,
		.flags    = 0,
	}, {
		.name     = "32 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.depth    = 32,
		.flags    = 0,
	}, {
		.name     = "24 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.depth    = 24,
		.flags    = 0,
	}, {
		.name     = "24 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB24,
		.depth    = 24,
		.flags    = 0,
	}, {
		.name     = "4:2:2, packed, YUYV",
	       .fourcc    = V4L2_PIX_FMT_YUYV,
	       .depth     = 16,
	       .flags     = 0,
	}, {
		.name     = "4:2:2, packed, YUYV",
	       .fourcc    = V4L2_PIX_FMT_YUYV,
	       .depth     = 16,
	       .flags     = 0,
	}, {
		.name     = "4:2:2, packed, UYVY",
	       .fourcc    = V4L2_PIX_FMT_UYVY,
	       .depth     = 16,
	       .flags     = 0,
	}, {
#ifdef V4L2_PIX_FMT_YVYU
		.name     = "4:2:2, packed YVYU",
	       .fourcc    = V4L2_PIX_FMT_YVYU,
	       .depth     = 16,
	       .flags     = 0,
	}, {
#endif
#ifdef V4L2_PIX_FMT_VYUY
		.name     = "4:2:2, packed VYUY",
	       .fourcc    = V4L2_PIX_FMT_VYUY,
	       .depth     = 16,
	       .flags     = 0,
	}, {
#endif
		.name     = "4:2:2, packed YYUV",
	       .fourcc    = V4L2_PIX_FMT_YYUV,
	       .depth     = 16,
	       .flags     = 0,
	}, {
		.name     = "YUV-8-8-8-8",
	       .fourcc    = V4L2_PIX_FMT_YUV32,
	       .depth     = 32,
	       .flags     = 0,
	}, {
		.name     = "8 bpp, gray",
	       .fourcc    = V4L2_PIX_FMT_GREY,
	       .depth     = 8,
	       .flags     = 0,
	}, {
		.name     = "16 Greyscale",
	       .fourcc    = V4L2_PIX_FMT_Y16,
	       .depth     = 16,
	       .flags     = 0,
	},

	/* here come the planar formats */
	{
		.name     = "4:1:0, planar, Y-Cr-Cb",
		.fourcc   = V4L2_PIX_FMT_YVU410,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR,
	}, {
		.name     = "4:2:0, planar, Y-Cr-Cb",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
	}, {
		.name     = "4:1:0, planar, Y-Cb-Cr",
		.fourcc   = V4L2_PIX_FMT_YUV410,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR,
	}, {
		.name     = "4:2:0, planar, Y-Cb-Cr",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,

	/* here come the compressed formats */

#ifdef V4L2_PIX_FMT_MJPEG
	}, {
		.name     = "Motion-JPEG",
		.fourcc   = V4L2_PIX_FMT_MJPEG,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_MJPEG */
#ifdef V4L2_PIX_FMT_JPEG
	}, {
		.name     = "JFIF JPEG",
		.fourcc   = V4L2_PIX_FMT_JPEG,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_JPEG */
#ifdef V4L2_PIX_FMT_DV
	}, {
		.name     = "1394",
		.fourcc   = V4L2_PIX_FMT_DV,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_DV */
#ifdef V4L2_PIX_FMT_MPEG
	}, {
		.name     = "MPEG-1/2/4 Multiplexed",
		.fourcc   = V4L2_PIX_FMT_MPEG,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_MPEG */
#ifdef V4L2_PIX_FMT_H264
	}, {
		.name     = "H264 with start codes",
		.fourcc   = V4L2_PIX_FMT_H264,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_H264 */
#ifdef V4L2_PIX_FMT_H264_NO_SC
	}, {
		.name     = "H264 without start codes",
		.fourcc   = V4L2_PIX_FMT_H264_NO_SC,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_H264_NO_SC */
#ifdef V4L2_PIX_FMT_H264_MVC
	}, {
		.name     = "H264 MVC",
		.fourcc   = V4L2_PIX_FMT_H264_MVC,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_H264_MVC */
#ifdef V4L2_PIX_FMT_H263
	}, {
		.name     = "H263",
		.fourcc   = V4L2_PIX_FMT_H263,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_H263 */
#ifdef V4L2_PIX_FMT_MPEG1
	}, {
		.name     = "MPEG-1 ES",
		.fourcc   = V4L2_PIX_FMT_MPEG1,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_MPEG1 */
#ifdef V4L2_PIX_FMT_MPEG2
	}, {
		.name     = "MPEG-2 ES",
		.fourcc   = V4L2_PIX_FMT_MPEG2,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_MPEG2 */
#ifdef V4L2_PIX_FMT_MPEG4
	}, {
		.name     = "MPEG-4 part 2 ES",
		.fourcc   = V4L2_PIX_FMT_MPEG4,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_MPEG4 */
#ifdef V4L2_PIX_FMT_XVID
	}, {
		.name     = "Xvid",
		.fourcc   = V4L2_PIX_FMT_XVID,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_XVID */
#ifdef V4L2_PIX_FMT_VC1_ANNEX_G
	}, {
		.name     = "SMPTE 421M Annex G compliant stream",
		.fourcc   = V4L2_PIX_FMT_VC1_ANNEX_G,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_VC1_ANNEX_G */
#ifdef V4L2_PIX_FMT_VC1_ANNEX_L
	}, {
		.name     = "SMPTE 421M Annex L compliant stream",
		.fourcc   = V4L2_PIX_FMT_VC1_ANNEX_L,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_VC1_ANNEX_L */
#ifdef V4L2_PIX_FMT_VP8
	}, {
		.name     = "VP8",
		.fourcc   = V4L2_PIX_FMT_VP8,
		.depth    = 0,
		.flags    = FORMAT_FLAGS_COMPRESSED,
#endif /* V4L2_PIX_FMT_VP8 */
