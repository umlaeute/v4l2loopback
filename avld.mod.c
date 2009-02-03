#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x36ef467a, "struct_module" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xa5423cc4, "param_get_int" },
	{ 0xd1a46ee3, "no_llseek" },
	{ 0x73bcd634, "mutex_unlock" },
	{ 0x999e8297, "vfree" },
	{ 0xcb32da10, "param_set_int" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x28ef23ed, "video_register_device" },
	{ 0x3744cf36, "vmalloc_to_pfn" },
	{ 0x7b9eb732, "mutex_lock_interruptible" },
	{ 0x91d6536d, "__mutex_init" },
	{ 0x1b7d4074, "printk" },
	{ 0x859204af, "sscanf" },
	{ 0x21b1d236, "video_unregister_device" },
	{ 0xffd3c7, "init_waitqueue_head" },
	{ 0x72270e35, "do_gettimeofday" },
	{ 0xbbc366f2, "remap_pfn_range" },
	{ 0x67f36b7c, "interruptible_sleep_on_timeout" },
	{ 0x5ed90a7d, "v4l_compat_ioctl32" },
	{ 0xf2a644fb, "copy_from_user" },
	{ 0xe914e41e, "strcpy" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=videodev,compat_ioctl32";


MODULE_INFO(srcversion, "1F3D2288CF66502047B595F");
