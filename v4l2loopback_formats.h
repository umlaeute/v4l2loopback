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
