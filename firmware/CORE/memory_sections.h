#ifndef __MEMORY_SECTIONS_H__
#define __MEMORY_SECTIONS_H__

#if defined(__CC_ARM)
#define FW_RAMFUNC0        __attribute__((noinline, section("RAM0_CODE")))
#define FW_RAMFUNC1        __attribute__((noinline, section("RAM1_CODE")))
#define FW_RAM0_RO         __attribute__((section("RAM0_RO")))
#define FW_RAM1_RO         __attribute__((section("RAM1_RO")))
#define FW_RAM0_RW         __attribute__((section("RAM0_RW")))
#define FW_RAM1_RW         __attribute__((section("RAM1_RW")))
#define FW_RAM0_ZI         __attribute__((section("RAM0_ZI"), zero_init))
#define FW_RAM1_ZI         __attribute__((section("RAM1_ZI"), zero_init))
#else
#define FW_RAMFUNC0
#define FW_RAMFUNC1
#define FW_RAM0_RO
#define FW_RAM1_RO
#define FW_RAM0_RW
#define FW_RAM1_RW
#define FW_RAM0_ZI
#define FW_RAM1_ZI
#endif

#endif
