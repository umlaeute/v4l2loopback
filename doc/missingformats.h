#ifdef V4L2_PIX_FMT_Y10BPACK
	}, {
		.name     = "10 bpp Greyscale bit-packed",
		.fourcc   = V4L2_PIX_FMT_Y10BPACK,
		.depth    = 10,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_Y10BPACK */
#ifdef V4L2_PIX_FMT_PAL8
	}, {
		.name     = "8 bpp 8-bit palette",
		.fourcc   = V4L2_PIX_FMT_PAL8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_PAL8 */
#ifdef V4L2_PIX_FMT_UV8
	}, {
		.name     = "8 bpp UV 4:4",
		.fourcc   = V4L2_PIX_FMT_UV8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_UV8 */

#ifdef V4L2_PIX_FMT_HI240
	}, {
		.name     = "8 bpp 8-bit color  ",
		.fourcc   = V4L2_PIX_FMT_HI240,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PLANAR,
#endif /* V4L2_PIX_FMT_HI240 */
#ifdef V4L2_PIX_FMT_HM12
	}, {
		.name     = "8 bpp YUV 4:2:0 16x16 macroblocks",
		.fourcc   = V4L2_PIX_FMT_HM12,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PLANAR,
#endif /* V4L2_PIX_FMT_HM12 */
#ifdef V4L2_PIX_FMT_M420
	}, {
		.name     = "12 bpp YUV 4:2:0 2 lines y, 1 line uv interleaved",
		.fourcc   = V4L2_PIX_FMT_M420,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
#endif /* V4L2_PIX_FMT_M420 */



#ifdef V4L2_PIX_FMT_NV12
	}, {
		.name     = "12 bpp Y/CbCr 4:2:0 ",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV12 */
#ifdef V4L2_PIX_FMT_NV21
	}, {
		.name     = "12 bpp Y/CrCb 4:2:0 ",
		.fourcc   = V4L2_PIX_FMT_NV21,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV21 */
#ifdef V4L2_PIX_FMT_NV16
	}, {
		.name     = "16 bpp Y/CbCr 4:2:2 ",
		.fourcc   = V4L2_PIX_FMT_NV16,
		.depth    = 16,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV16 */
#ifdef V4L2_PIX_FMT_NV61
	}, {
		.name     = "16 bpp Y/CrCb 4:2:2 ",
		.fourcc   = V4L2_PIX_FMT_NV61,
		.depth    = 16,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV61 */
#ifdef V4L2_PIX_FMT_NV24
	}, {
		.name     = "24 bpp Y/CbCr 4:4:4 ",
		.fourcc   = V4L2_PIX_FMT_NV24,
		.depth    = 24,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV24 */
#ifdef V4L2_PIX_FMT_NV42
	}, {
		.name     = "24 bpp Y/CrCb 4:4:4 ",
		.fourcc   = V4L2_PIX_FMT_NV42,
		.depth    = 24,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV42 */
#ifdef V4L2_PIX_FMT_NV12M
	}, {
		.name     = "12 bpp Y/CbCr 4:2:0 ",
		.fourcc   = V4L2_PIX_FMT_NV12M,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV12M */
#ifdef V4L2_PIX_FMT_NV21M
	}, {
		.name     = "21 bpp Y/CrCb 4:2:0 ",
		.fourcc   = V4L2_PIX_FMT_NV21M,
		.depth    = 21,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV21M */
#ifdef V4L2_PIX_FMT_NV16M
	}, {
		.name     = "16 bpp Y/CbCr 4:2:2 ",
		.fourcc   = V4L2_PIX_FMT_NV16M,
		.depth    = 16,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV16M */
#ifdef V4L2_PIX_FMT_NV61M
	}, {
		.name     = "16 bpp Y/CrCb 4:2:2 ",
		.fourcc   = V4L2_PIX_FMT_NV61M,
		.depth    = 16,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV61M */
#ifdef V4L2_PIX_FMT_NV12MT
	}, {
		.name     = "12 bpp Y/CbCr 4:2:0 64x32 macroblocks",
		.fourcc   = V4L2_PIX_FMT_NV12MT,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV12MT */
#ifdef V4L2_PIX_FMT_NV12MT_16X16
	}, {
		.name     = "12 bpp Y/CbCr 4:2:0 16x16 macroblocks",
		.fourcc   = V4L2_PIX_FMT_NV12MT_16X16,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_NV12MT_16X16 */
#ifdef V4L2_PIX_FMT_YUV420M
	}, {
		.name     = "12 bpp YUV420 planar",
		.fourcc   = V4L2_PIX_FMT_YUV420M,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_YUV420M */
#ifdef V4L2_PIX_FMT_YVU420M
	}, {
		.name     = "12 bpp YVU420 planar",
		.fourcc   = V4L2_PIX_FMT_YVU420M,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_YVU420M */
#ifdef V4L2_PIX_FMT_SBGGR8
	}, {
		.name     = "8 bpp BGBG.. GRGR..",
		.fourcc   = V4L2_PIX_FMT_SBGGR8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR8 */
#ifdef V4L2_PIX_FMT_SGBRG8
	}, {
		.name     = "8 bpp GBGB.. RGRG..",
		.fourcc   = V4L2_PIX_FMT_SGBRG8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGBRG8 */
#ifdef V4L2_PIX_FMT_SGRBG8
	}, {
		.name     = "8 bpp GRGR.. BGBG..",
		.fourcc   = V4L2_PIX_FMT_SGRBG8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGRBG8 */
#ifdef V4L2_PIX_FMT_SRGGB8
	}, {
		.name     = "8 bpp RGRG.. GBGB..",
		.fourcc   = V4L2_PIX_FMT_SRGGB8,
		.depth    = 8,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SRGGB8 */
#ifdef V4L2_PIX_FMT_SBGGR10
	}, {
		.name     = "10 bpp BGBG.. GRGR..",
		.fourcc   = V4L2_PIX_FMT_SBGGR10,
		.depth    = 10,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR10 */
#ifdef V4L2_PIX_FMT_SGBRG10
	}, {
		.name     = "10 bpp GBGB.. RGRG..",
		.fourcc   = V4L2_PIX_FMT_SGBRG10,
		.depth    = 10,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGBRG10 */
#ifdef V4L2_PIX_FMT_SGRBG10
	}, {
		.name     = "10 bpp GRGR.. BGBG..",
		.fourcc   = V4L2_PIX_FMT_SGRBG10,
		.depth    = 10,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGRBG10 */
#ifdef V4L2_PIX_FMT_SRGGB10
	}, {
		.name     = "10 bpp RGRG.. GBGB..",
		.fourcc   = V4L2_PIX_FMT_SRGGB10,
		.depth    = 10,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SRGGB10 */
#ifdef V4L2_PIX_FMT_SBGGR12
	}, {
		.name     = "12 bpp BGBG.. GRGR..",
		.fourcc   = V4L2_PIX_FMT_SBGGR12,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR12 */
#ifdef V4L2_PIX_FMT_SGBRG12
	}, {
		.name     = "12 bpp GBGB.. RGRG..",
		.fourcc   = V4L2_PIX_FMT_SGBRG12,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGBRG12 */
#ifdef V4L2_PIX_FMT_SGRBG12
	}, {
		.name     = "12 bpp GRGR.. BGBG..",
		.fourcc   = V4L2_PIX_FMT_SGRBG12,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGRBG12 */
#ifdef V4L2_PIX_FMT_SRGGB12
	}, {
		.name     = "12 bpp RGRG.. GBGB..",
		.fourcc   = V4L2_PIX_FMT_SRGGB12,
		.depth    = 12,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SRGGB12 */
#ifdef V4L2_PIX_FMT_SBGGR10ALAW8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SBGGR10ALAW8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR10ALAW8 */
#ifdef V4L2_PIX_FMT_SGBRG10ALAW8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SGBRG10ALAW8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGBRG10ALAW8 */
#ifdef V4L2_PIX_FMT_SGRBG10ALAW8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SGRBG10ALAW8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGRBG10ALAW8 */
#ifdef V4L2_PIX_FMT_SRGGB10ALAW8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SRGGB10ALAW8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SRGGB10ALAW8 */
#ifdef V4L2_PIX_FMT_SBGGR10DPCM8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SBGGR10DPCM8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR10DPCM8 */
#ifdef V4L2_PIX_FMT_SGBRG10DPCM8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SGBRG10DPCM8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGBRG10DPCM8 */
#ifdef V4L2_PIX_FMT_SGRBG10DPCM8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SGRBG10DPCM8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SGRBG10DPCM8 */
#ifdef V4L2_PIX_FMT_SRGGB10DPCM8
	}, {
		.name     = "",
		.fourcc   = V4L2_PIX_FMT_SRGGB10DPCM8,
		.depth    = 0,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SRGGB10DPCM8 */
#ifdef V4L2_PIX_FMT_SBGGR16
	}, {
		.name     = "16 bpp BGBG.. GRGR..",
		.fourcc   = V4L2_PIX_FMT_SBGGR16,
		.depth    = 16,
		.flags    = 0,
#endif /* V4L2_PIX_FMT_SBGGR16 */
