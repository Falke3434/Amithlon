/*
 *  $Id: powernow-k7.h,v 1.2 2003/02/10 18:26:01 davej Exp $
 *  (C) 2003 Dave Jones.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  AMD-specific information
 *
 */

#ifndef MSR_K7_FID_VID_CTL
#define MSR_K7_FID_VID_CTL		0xc0010041
#endif
#ifndef MSR_K7_FID_VID_STATUS
#define MSR_K7_FID_VID_STATUS	0xc0010042
#endif


union msr_fidvidctl {
	struct {
		unsigned FID:5,			// 4:0
		reserved1:3,	// 7:5
		VID:5,			// 12:8
		reserved2:3,	// 15:13
		FIDC:1,			// 16
		VIDC:1,			// 17
		reserved3:2,	// 19:18
		FIDCHGRATIO:1,	// 20
		reserved4:11,	// 31-21
		SGTC:20,		// 32:51
		reserved5:12;	// 63:52
	} bits;
	unsigned long long val;
};

union msr_fidvidstatus {
	struct {
		unsigned CFID:5,			// 4:0
		reserved1:3,	// 7:5
		SFID:5,			// 12:8
		reserved2:3,	// 15:13
		MFID:5,			// 20:16
		reserved3:11,	// 31:21
		CVID:5,			// 36:32
		reserved4:3,	// 39:37
		SVID:5,			// 44:40
		reserved5:3,	// 47:45
		MVID:5,			// 52:48
		reserved6:11;	// 63:53
	} bits;
	unsigned long long val;
};
