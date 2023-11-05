
UCHAR SiS300_OEMTVDelay[8][4]={
{0x08,0x08,0x08,0x08},
{0x08,0x08,0x08,0x08},
{0x08,0x08,0x08,0x08},
{0x2c,0x2c,0x2c,0x2c},
{0x08,0x08,0x08,0x08},
{0x08,0x08,0x08,0x08},
{0x08,0x08,0x08,0x08},
{0x20,0x20,0x20,0x20}};

UCHAR SiS300_OEMTVFlicker[8][4]={
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00}};

UCHAR SiS300_OEMLCDDelay1[12][4]={
{0x2c,0x2c,0x2c,0x2c},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x2c,0x2c,0x2c,0x2c},
{0x2c,0x2c,0x2c,0x2c},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x24,0x24,0x24,0x24},
{0x24,0x24,0x24,0x24},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x24,0x24,0x24,0x24}};


UCHAR SiS300_OEMLCDDelay2[32][4]={
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20},
{0x20,0x20,0x20,0x20}};


/*
UCHAR SiS300_StNTSCDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_StPALDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_StSCARTDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_StHiTVDelay[]={0x2c,0x2c,0x2c,0x2c,0xff};
UCHAR SiS300_ExtNTSCDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_ExtPALDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_ExtSCARTDelay[]={0x08,0x08,0x08,0x08,0xff};
UCHAR SiS300_ExtHiTVDelay[]={0x20,0x20,0x20,0x20,0xff};
UCHAR SiS300_StNTSCFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_StPALFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_StSCARTFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_StHiTVFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_ExtNTSCFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_ExtPALFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_ExtSCARTFlicker[]={0x00,0x00,0x00,0x00,0xff};
UCHAR SiS300_ExtHiTVFlicker[]={0x00,0x00,0x00,0x00,0xff};
*/
#if 0
//typedef struct _SiS_OEMTVPhasestruct  {
//UCHAR Index[4];
//} SiS_OEMTVPhasestruct;
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_StNTSCPhase[6][4]={
{0x21,0xed,0x00,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_StPALPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_StSCARTPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_StHiTVPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_ExtNTSCPhase[6][4]={
{0x21,0xed,0x00,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0x21,0xed,0x8a,0x08},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_ExtPALPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_ExtSCARTPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVPhasestruct
#endif
UCHAR SiS300_ExtHiTVPhase[6][4]={
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0x2a,0x05,0xd3,0x00},
{0xff,0xff,0xff,0xff}};
#if 0
//typedef struct _SiS_OEMTVFilterstruct{
//UCHAR Index[4];
//} SiS_OEMTVFilterstruct;
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_StNTSCFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x10,0x18},
{0xf7,0x06,0x19,0x14},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x15,0x25,0xf6},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_StPALFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_StSCARTFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_StHiTVFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_ExtNTSCFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x10,0x18},
{0xf7,0x06,0x19,0x14},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x15,0x25,0xf6},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_ExtPALFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_ExtSCARTFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//SiS_OEMTVFilterstruct
#endif
UCHAR SiS300_ExtHiTVFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x10,0x32},
{0xf3,0x00,0x1d,0x20},
{0x00,0xf4,0x10,0x38},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xfc,0xfb,0x14,0x2a},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xf1,0xf7,0x1f,0x32},
{0xff,0xff,0xff,0xff}};
#if 0
//typedef struct _SiS_OEMTVFilter2struct{
//UCHAR Index[7];
//} SiS_OEMTVFilter2struct;
//SiS_OEMTVFilter2struct
#endif
UCHAR SiS300_NTSCFilter2[9][7]={
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0x01,0x01,0xFC,0xF8,0x08,0x26,0x38},
{0xFF,0xFF,0xFC,0x00,0x0F,0x22,0x28}};
#if 0
//SiS_OEMTVFilter2struct
#endif
UCHAR SiS300_PALFilter2[9][7]={
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0x01,0x01,0xFC,0xF8,0x08,0x26,0x38},
{0xFF,0xFF,0xFC,0x00,0x0F,0x22,0x28}};

UCHAR SiS300_PALMFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x10,0x18},
{0xf7,0x06,0x19,0x14},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x15,0x25,0xf6},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xff,0xff,0xff,0xff}};
UCHAR SiS300_PALNFilter[17][4]={
{0x00,0xf4,0x10,0x38},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x10,0x18},
{0xf7,0x06,0x19,0x14},
{0x00,0xf4,0x10,0x38},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x15,0x25,0xf6},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xeb,0x04,0x25,0x18},
{0xff,0xff,0xff,0xff}};


UCHAR SiS300_PALMFilter2[9][7]={
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0x01,0x01,0xFC,0xF8,0x08,0x26,0x38},
{0xFF,0xFF,0xFC,0x00,0x0F,0x22,0x28}};

UCHAR SiS300_PALNFilter2[9][7]={
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0xFF,0x03,0x02,0xF6,0xFC,0x27,0x46},
{0x01,0x02,0xFE,0xF7,0x03,0x27,0x3C},
{0x01,0x01,0xFC,0xF8,0x08,0x26,0x38},
{0xFF,0xFF,0xFC,0x00,0x0F,0x22,0x28}};
