#ifndef FPTEST_H
#define FPTEST_H

#ifdef __KERNEL__
#include <linux/module.h>
#else /* !__KERNEL__ */
#include <stdio.h>
#define printk printf
#endif /* !__KERNEL__ */

#define FP_REG_SET(reg) asm ("ldfd f" #reg " = %0" : /* no output. */ : "m"(val))

#define FP_REG_CHECK(reg) asm ("stfd %0 = f" #reg: "=m"(e[reg-1]))

static inline void fp_regs_set(unsigned long val)
{
	FP_REG_SET(15); FP_REG_SET(16); FP_REG_SET(17); FP_REG_SET(18);
	FP_REG_SET(19); FP_REG_SET(20); FP_REG_SET(21); FP_REG_SET(22);
	FP_REG_SET(23); FP_REG_SET(24); FP_REG_SET(25); FP_REG_SET(26);
	FP_REG_SET(27); FP_REG_SET(28); FP_REG_SET(29); FP_REG_SET(30);
	FP_REG_SET(31); FP_REG_SET(32); FP_REG_SET(33); FP_REG_SET(34);
	FP_REG_SET(35); FP_REG_SET(36); FP_REG_SET(37); FP_REG_SET(38);
	FP_REG_SET(39); FP_REG_SET(40); FP_REG_SET(41); FP_REG_SET(42);
	FP_REG_SET(43); FP_REG_SET(44); FP_REG_SET(45); FP_REG_SET(46);
	FP_REG_SET(47); FP_REG_SET(48); FP_REG_SET(49); FP_REG_SET(50);
	FP_REG_SET(51); FP_REG_SET(52); FP_REG_SET(53); FP_REG_SET(54);
	FP_REG_SET(55); FP_REG_SET(56); FP_REG_SET(57); FP_REG_SET(58);
	FP_REG_SET(59); FP_REG_SET(60); FP_REG_SET(61); FP_REG_SET(62);
	FP_REG_SET(63); FP_REG_SET(64); FP_REG_SET(65); FP_REG_SET(66);
	FP_REG_SET(67); FP_REG_SET(68); FP_REG_SET(69); FP_REG_SET(70);
	FP_REG_SET(71); FP_REG_SET(72); FP_REG_SET(73); FP_REG_SET(74);
	FP_REG_SET(75); FP_REG_SET(76); FP_REG_SET(77); FP_REG_SET(78);
	FP_REG_SET(79); FP_REG_SET(80); FP_REG_SET(81); FP_REG_SET(82);
	FP_REG_SET(83); FP_REG_SET(84); FP_REG_SET(85); FP_REG_SET(86);
	FP_REG_SET(87); FP_REG_SET(88); FP_REG_SET(89); FP_REG_SET(90);
	FP_REG_SET(91); FP_REG_SET(92); FP_REG_SET(93); FP_REG_SET(94);
	FP_REG_SET(95); FP_REG_SET(96); FP_REG_SET(97); FP_REG_SET(98);
	FP_REG_SET(99); FP_REG_SET(100); FP_REG_SET(101); FP_REG_SET(102);
	FP_REG_SET(103); FP_REG_SET(104); FP_REG_SET(105); FP_REG_SET(106);
	FP_REG_SET(107); FP_REG_SET(108); FP_REG_SET(109); FP_REG_SET(110);
	FP_REG_SET(111); FP_REG_SET(112); FP_REG_SET(113); FP_REG_SET(114);
	FP_REG_SET(115); FP_REG_SET(116); FP_REG_SET(117); FP_REG_SET(118);
	FP_REG_SET(119); FP_REG_SET(120); FP_REG_SET(121); FP_REG_SET(122);
	FP_REG_SET(123); FP_REG_SET(124); FP_REG_SET(125); FP_REG_SET(126);
	FP_REG_SET(127);
}

static inline unsigned fp_regs_check(unsigned long val)
{
	unsigned long e[128], result = val;
	unsigned i;

	FP_REG_CHECK(15); FP_REG_CHECK(16); FP_REG_CHECK(17); FP_REG_CHECK(18);
	FP_REG_CHECK(19); FP_REG_CHECK(20); FP_REG_CHECK(21); FP_REG_CHECK(22);
	FP_REG_CHECK(23); FP_REG_CHECK(24); FP_REG_CHECK(25); FP_REG_CHECK(26);
	FP_REG_CHECK(27); FP_REG_CHECK(28); FP_REG_CHECK(29); FP_REG_CHECK(30);
	FP_REG_CHECK(31); FP_REG_CHECK(32); FP_REG_CHECK(33); FP_REG_CHECK(34);
	FP_REG_CHECK(35); FP_REG_CHECK(36); FP_REG_CHECK(37); FP_REG_CHECK(38);
	FP_REG_CHECK(39); FP_REG_CHECK(40); FP_REG_CHECK(41); FP_REG_CHECK(42);
	FP_REG_CHECK(43); FP_REG_CHECK(44); FP_REG_CHECK(45); FP_REG_CHECK(46);
	FP_REG_CHECK(47); FP_REG_CHECK(48); FP_REG_CHECK(49); FP_REG_CHECK(50);
	FP_REG_CHECK(51); FP_REG_CHECK(52); FP_REG_CHECK(53); FP_REG_CHECK(54);
	FP_REG_CHECK(55); FP_REG_CHECK(56); FP_REG_CHECK(57); FP_REG_CHECK(58);
	FP_REG_CHECK(59); FP_REG_CHECK(60); FP_REG_CHECK(61); FP_REG_CHECK(62);
	FP_REG_CHECK(63); FP_REG_CHECK(64); FP_REG_CHECK(65); FP_REG_CHECK(66);
	FP_REG_CHECK(67); FP_REG_CHECK(68); FP_REG_CHECK(69); FP_REG_CHECK(70);
	FP_REG_CHECK(71); FP_REG_CHECK(72); FP_REG_CHECK(73); FP_REG_CHECK(74);
	FP_REG_CHECK(75); FP_REG_CHECK(76); FP_REG_CHECK(77); FP_REG_CHECK(78);
	FP_REG_CHECK(79); FP_REG_CHECK(80); FP_REG_CHECK(81); FP_REG_CHECK(82);
	FP_REG_CHECK(83); FP_REG_CHECK(84); FP_REG_CHECK(85); FP_REG_CHECK(86);
	FP_REG_CHECK(87); FP_REG_CHECK(88); FP_REG_CHECK(89); FP_REG_CHECK(90);
	FP_REG_CHECK(91); FP_REG_CHECK(92); FP_REG_CHECK(93); FP_REG_CHECK(94);
	FP_REG_CHECK(95); FP_REG_CHECK(96); FP_REG_CHECK(97); FP_REG_CHECK(98);
	FP_REG_CHECK(99); FP_REG_CHECK(100); FP_REG_CHECK(101);FP_REG_CHECK(102);
	FP_REG_CHECK(103); FP_REG_CHECK(104);FP_REG_CHECK(105);FP_REG_CHECK(106);
	FP_REG_CHECK(107); FP_REG_CHECK(108);FP_REG_CHECK(109);FP_REG_CHECK(110);
	FP_REG_CHECK(111); FP_REG_CHECK(112);FP_REG_CHECK(113);FP_REG_CHECK(114);
	FP_REG_CHECK(115); FP_REG_CHECK(116);FP_REG_CHECK(117);FP_REG_CHECK(118);
	FP_REG_CHECK(119); FP_REG_CHECK(120);FP_REG_CHECK(121);FP_REG_CHECK(122);
	FP_REG_CHECK(123); FP_REG_CHECK(124);FP_REG_CHECK(125);FP_REG_CHECK(126);
	FP_REG_CHECK(127);

	for (i = 14; i < 127; i++)
		if (e[i] != val) {
			printk("f%d= %lu != %lu\n", i + 1, e[i], val);
			result = e[i];
		}
	return result;
}

#endif /* FPTEST_H */
