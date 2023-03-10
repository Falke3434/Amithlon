#ifndef __KERNEL__
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/hdreg.h>
#include <linux/version.h>
#include <asm/dasd.h>
#endif

#define DASD_API_VERSION 0

#define LINE_LENGTH 80
#define VTOC_START_CC 0x0
#define VTOC_START_HH 0x1
#define FIRST_USABLE_CYL 1
#define FIRST_USABLE_TRK 2

#define DASD_3380_TYPE 13148
#define DASD_3390_TYPE 13200
#define DASD_9345_TYPE 37701

#define DASD_3380_VALUE 0xbb60
#define DASD_3390_VALUE 0xe5a2
#define DASD_9345_VALUE 0xbc98

#define VOLSER_LENGTH 6
#define BIG_DISK_SIZE 0x10000

#define VTOC_ERROR "VTOC error:"

enum failure {unable_to_open,
	      unable_to_seek,
	      unable_to_write,
	      unable_to_read};

unsigned char ASCtoEBC[256] =
{
  /*00  NL    SH    SX    EX    ET    NQ    AK    BL */
  0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F,
  /*08  BS    HT    LF    VT    FF    CR    SO    SI */
  0x16, 0x05, 0x15, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  /*10  DL    D1    D2    D3    D4    NK    SN    EB */
  0x10, 0x11, 0x12, 0x13, 0x3C, 0x15, 0x32, 0x26,
  /*18  CN    EM    SB    EC    FS    GS    RS    US */
  0x18, 0x19, 0x3F, 0x27, 0x1C, 0x1D, 0x1E, 0x1F,
  /*20  SP     !     "     #     $     %     &     ' */
  0x40, 0x5A, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D,
  /*28   (     )     *     +     ,     -    .      / */
  0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
  /*30   0     1     2     3     4     5     6     7 */
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
  /*38   8     9     :     ;     <     =     >     ? */
  0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
  /*40   @     A     B     C     D     E     F     G */
  0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
  /*48   H     I     J     K     L     M     N     O */
  0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
  /*50   P     Q     R     S     T     U     V     W */
  0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
  /*58   X     Y     Z     [     \     ]     ^     _ */
  0xE7, 0xE8, 0xE9, 0xAD, 0xE0, 0xBD, 0x5F, 0x6D,
  /*60   `     a     b     c     d     e     f     g */
  0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  /*68   h     i     j     k     l     m     n     o */
  0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
  /*70   p     q     r     s     t     u     v     w */
  0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
  /*78   x     y     z     {     |     }     ~    DL */
  0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0xA1, 0x07,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
  0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0xFF
};


unsigned char EBCtoASC[256] =
{
 /* 0x00   NUL   SOH   STX   ETX  *SEL    HT  *RNL   DEL */
          0x00, 0x01, 0x02, 0x03, 0x07, 0x09, 0x07, 0x7F,
 /* 0x08   -GE  -SPS  -RPT    VT    FF    CR    SO    SI */
          0x07, 0x07, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 /* 0x10   DLE   DC1   DC2   DC3  -RES   -NL    BS  -POC
                                  -ENP  ->LF             */
          0x10, 0x11, 0x12, 0x13, 0x07, 0x0A, 0x08, 0x07,
 /* 0x18   CAN    EM  -UBS  -CU1  -IFS  -IGS  -IRS  -ITB
                                                    -IUS */
          0x18, 0x19, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
 /* 0x20   -DS  -SOS    FS  -WUS  -BYP    LF   ETB   ESC
                                  -INP                   */
          0x07, 0x07, 0x1C, 0x07, 0x07, 0x0A, 0x17, 0x1B,
 /* 0x28   -SA  -SFE   -SM  -CSP  -MFA   ENQ   ACK   BEL
                       -SW                               */ 
          0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06, 0x07,
 /* 0x30  ----  ----   SYN   -IR   -PP  -TRN  -NBS   EOT */
          0x07, 0x07, 0x16, 0x07, 0x07, 0x07, 0x07, 0x04,
 /* 0x38  -SBS   -IT  -RFF  -CU3   DC4   NAK  ----   SUB */
          0x07, 0x07, 0x07, 0x07, 0x14, 0x15, 0x07, 0x1A,
 /* 0x40    SP   RSP           ?              ----       */
          0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0x07, 0x86,
 /* 0x48                       .     <     (     +     | */
          0x87, 0xA4, 0x9B, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
 /* 0x50     &                                      ---- */
          0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x07,
 /* 0x58           ?     !     $     *     )     ;       */
          0x8D, 0xE1, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0xAA,
 /* 0x60     -     /  ----     ?  ----  ----  ----       */
          0x2D, 0x2F, 0x07, 0x8E, 0x07, 0x07, 0x07, 0x8F,
 /* 0x68              ----     ,     %     _     >     ? */ 
          0x80, 0xA5, 0x07, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
 /* 0x70  ----        ----  ----  ----  ----  ----  ---- */
          0x07, 0x90, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
 /* 0x78     *     `     :     #     @     '     =     " */
          0x70, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
 /* 0x80     *     a     b     c     d     e     f     g */
          0x07, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
 /* 0x88     h     i              ----  ----  ----       */
          0x68, 0x69, 0xAE, 0xAF, 0x07, 0x07, 0x07, 0xF1,
 /* 0x90     ?     j     k     l     m     n     o     p */
          0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
 /* 0x98     q     r                    ----        ---- */
          0x71, 0x72, 0xA6, 0xA7, 0x91, 0x07, 0x92, 0x07,
 /* 0xA0           ~     s     t     u     v     w     x */
          0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
 /* 0xA8     y     z              ----  ----  ----  ---- */
          0x79, 0x7A, 0xAD, 0xAB, 0x07, 0x07, 0x07, 0x07,
 /* 0xB0     ^                    ----     ?  ----       */
          0x5E, 0x9C, 0x9D, 0xFA, 0x07, 0x07, 0x07, 0xAC,
 /* 0xB8        ----     [     ]  ----  ----  ----  ---- */
          0xAB, 0x07, 0x5B, 0x5D, 0x07, 0x07, 0x07, 0x07,
 /* 0xC0     {     A     B     C     D     E     F     G */
          0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
 /* 0xC8     H     I  ----           ?              ---- */
          0x48, 0x49, 0x07, 0x93, 0x94, 0x95, 0xA2, 0x07,
 /* 0xD0     }     J     K     L     M     N     O     P */
          0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
 /* 0xD8     Q     R  ----           ?                   */
          0x51, 0x52, 0x07, 0x96, 0x81, 0x97, 0xA3, 0x98,
 /* 0xE0     \           S     T     U     V     W     X */
          0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
 /* 0xE8     Y     Z        ----     ?  ----  ----  ---- */
          0x59, 0x5A, 0xFD, 0x07, 0x99, 0x07, 0x07, 0x07,
 /* 0xF0     0     1     2     3     4     5     6     7 */
          0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
 /* 0xF8     8     9  ----  ----     ?  ----  ----  ---- */
          0x38, 0x39, 0x07, 0x07, 0x9A, 0x07, 0x07, 0x07
};

typedef struct ttr 
{
        __u16 tt;
        __u8  r;
} __attribute__ ((packed)) ttr_t;

typedef struct cchhb 
{
        __u16 cc;
        __u16 hh;
        __u8 b;
} __attribute__ ((packed)) cchhb_t;

typedef struct cchh 
{
        __u16 cc;
        __u16 hh;
} __attribute__ ((packed)) cchh_t;

typedef struct labeldate 
{
        __u8  year;
        __u16 day;
} __attribute__ ((packed)) labeldate_t;


typedef struct volume_label 
{
        char volkey[4];         /* volume key = volume label                 */
	char vollbl[4];	        /* volume label                              */
	char volid[6];	        /* volume identifier                         */
	__u8 security;	        /* security byte                             */
	cchhb_t vtoc;           /* VTOC address                              */
	char res1[5];	        /* reserved                                  */
        char cisize[4];	        /* CI-size for FBA,...                       */
                                /* ...blanks for CKD                         */
	char blkperci[4];       /* no of blocks per CI (FBA), blanks for CKD */
	char labperci[4];       /* no of labels per CI (FBA), blanks for CKD */
	char res2[4];	        /* reserved                                  */
	char lvtoc[14];	        /* owner code for LVTOC                      */
	char res3[29];	        /* reserved                                  */
} __attribute__ ((packed)) volume_label_t;


typedef struct extent 
{
        __u8  typeind;          /* extent type indicator                     */
        __u8  seqno;            /* extent sequence number                    */
        cchh_t llimit;          /* starting point of this extent             */
        cchh_t ulimit;          /* ending point of this extent               */
} __attribute__ ((packed)) extent_t;


typedef struct dev_const 
{
        __u16 DS4DSCYL;           /* number of logical cyls                  */
        __u16 DS4DSTRK;           /* number of tracks in a logical cylinder  */
        __u16 DS4DEVTK;           /* device track length                     */
        __u8  DS4DEVI;            /* non-last keyed record overhead          */
        __u8  DS4DEVL;            /* last keyed record overhead              */
        __u8  DS4DEVK;            /* non-keyed record overhead differential  */
        __u8  DS4DEVFG;           /* flag byte                               */
        __u16 DS4DEVTL;           /* device tolerance                        */
        __u8  DS4DEVDT;           /* number of DSCB's per track              */
        __u8  DS4DEVDB;           /* number of directory blocks per track    */
} __attribute__ ((packed)) dev_const_t;


typedef struct format1_label 
{
	char  DS1DSNAM[44];       /* data set name                           */
	__u8  DS1FMTID;           /* format identifier                       */
	char  DS1DSSN[6];         /* data set serial number                  */
	__u16 DS1VOLSQ;           /* volume sequence number                  */
	labeldate_t DS1CREDT;     /* creation date: ydd                      */
	labeldate_t DS1EXPDT;     /* expiration date                         */
	__u8  DS1NOEPV;           /* number of extents on volume             */
        __u8  DS1NOBDB;           /* no. of bytes used in last direction blk */
	__u8  DS1FLAG1;           /* flag 1                                  */
	char  DS1SYSCD[13];       /* system code                             */
	labeldate_t DS1REFD;      /* date last referenced                    */
        __u8  DS1SMSFG;           /* system managed storage indicators       */
        __u8  DS1SCXTF;           /* sec. space extension flag byte          */
        __u16 DS1SCXTV;           /* secondary space extension value         */
        __u8  DS1DSRG1;           /* data set organisation byte 1            */
        __u8  DS1DSRG2;           /* data set organisation byte 2            */
  	__u8  DS1RECFM;           /* record format                           */
	__u8  DS1OPTCD;           /* option code                             */
	__u16 DS1BLKL;            /* block length                            */
	__u16 DS1LRECL;           /* record length                           */
	__u8  DS1KEYL;            /* key length                              */
	__u16 DS1RKP;             /* relative key position                   */
	__u8  DS1DSIND;           /* data set indicators                     */
        __u8  DS1SCAL1;           /* secondary allocation flag byte          */
  	char DS1SCAL3[3];         /* secondary allocation quantity           */
	ttr_t DS1LSTAR;           /* last used track and block on track      */
	__u16 DS1TRBAL;           /* space remaining on last used track      */
        __u16 res1;               /* reserved                                */
	extent_t DS1EXT1;         /* first extent description                */
	extent_t DS1EXT2;         /* second extent description               */
	extent_t DS1EXT3;         /* third extent description                */
	cchhb_t DS1PTRDS;         /* possible pointer to f2 or f3 DSCB       */
} __attribute__ ((packed)) format1_label_t;


typedef struct format4_label 
{
	char  DS4KEYCD[44];       /* key code for VTOC labels: 44 times 0x04 */
        __u8  DS4IDFMT;           /* format identifier                       */
	cchhb_t DS4HPCHR;         /* highest address of a format 1 DSCB      */
        __u16 DS4DSREC;           /* number of available DSCB's              */
        cchh_t DS4HCCHH;          /* CCHH of next available alternate track  */
        __u16 DS4NOATK;           /* number of remaining alternate tracks    */
        __u8  DS4VTOCI;           /* VTOC indicators                         */
        __u8  DS4NOEXT;           /* number of extents in VTOC               */
        __u8  DS4SMSFG;           /* system managed storage indicators       */
        __u8  DS4DEVAC;           /* number of alternate cylinders. 
                                     Subtract from first two bytes of 
                                     DS4DEVSZ to get number of usable
				     cylinders. can be zero. valid
				     only if DS4DEVAV on.                    */
        dev_const_t DS4DEVCT;     /* device constants                        */
        char DS4AMTIM[8];         /* VSAM time stamp                         */
        char DS4AMCAT[3];         /* VSAM catalog indicator                  */
        char DS4R2TIM[8];         /* VSAM volume/catalog match time stamp    */
        char res1[5];             /* reserved                                */
        char DS4F6PTR[5];         /* pointer to first format 6 DSCB          */
        extent_t DS4VTOCE;        /* VTOC extent description                 */
        char res2[10];            /* reserved                                */
        __u8 DS4EFLVL;            /* extended free-space management level    */
        cchhb_t DS4EFPTR;         /* pointer to extended free-space info     */
        char res3[9];             /* reserved                                */
} __attribute__ ((packed)) format4_label_t;


typedef struct ds5ext 
{
	__u16 t;                  /* RTA of the first track of free extent   */
	__u16 fc;                 /* number of whole cylinders in free ext.  */
	__u8  ft;                 /* number of remaining free tracks         */
} __attribute__ ((packed)) ds5ext_t;


typedef struct format5_label 
{
	char DS5KEYID[4];         /* key identifier                          */
	ds5ext_t DS5AVEXT;        /* first available (free-space) extent.    */
	ds5ext_t DS5EXTAV[7];     /* seven available extents                 */
	__u8 DS5FMTID;            /* format identifier                       */
	ds5ext_t DS5MAVET[18];    /* eighteen available extents              */
	cchhb_t DS5PTRDS[5];      /* pointer to next format5 DSCB            */
} __attribute__ ((packed)) format5_label_t;


typedef struct ds7ext 
{
	__u32 a;                  /* starting RTA value                      */
	__u32 b;                  /* ending RTA value + 1                    */
} __attribute__ ((packed)) ds7ext_t;


typedef struct format7_label 
{
	char DS7KEYID[4];         /* key identifier                          */
	ds7ext_t DS7EXTNT[5];     /* space for 5 extent descriptions         */
	__u8 DS7FMTID;            /* format identifier                       */
	ds7ext_t DS7ADEXT[11];    /* space for 11 extent descriptions        */
	char res1[2];             /* reserved                                */
	cchhb_t DS7PTRDS;         /* pointer to next FMT7 DSCB               */
} __attribute__ ((packed)) format7_label_t;


char * vtoc_ebcdic_enc (
        unsigned char source[LINE_LENGTH],
        unsigned char target[LINE_LENGTH],
	int l);
char * vtoc_ebcdic_dec (
        unsigned char source[LINE_LENGTH],
	unsigned char target[LINE_LENGTH],
	int l);
void vtoc_set_extent (
        extent_t * ext,
        __u8 typeind,
        __u8 seqno,
        cchh_t * lower,
        cchh_t * upper);
void vtoc_set_cchh (
        cchh_t * addr,
	__u16 cc,
	__u16 hh);
void vtoc_set_cchhb (
        cchhb_t * addr,
        __u16 cc,
        __u16 hh,
        __u8 b);
void vtoc_set_date (
        labeldate_t * d,
        __u8 year,
        __u16 day);

void vtoc_volume_label_init (
	volume_label_t *vlabel);

int vtoc_read_volume_label (
        char * device,
        unsigned long vlabel_start,
        volume_label_t * vlabel);

int vtoc_write_volume_label (
        char *device,
        unsigned long vlabel_start,
        volume_label_t *vlabel);

void vtoc_volume_label_set_volser (
	volume_label_t *vlabel,
	char *volser);

char *vtoc_volume_label_get_volser (
	volume_label_t *vlabel,
	char *volser);

void vtoc_volume_label_set_key (
        volume_label_t *vlabel,
        char *key);     

void vtoc_volume_label_set_label (
	volume_label_t *vlabel,
	char *lbl);

char *vtoc_volume_label_get_label (
	volume_label_t *vlabel,
	char *lbl);

void vtoc_read_label (
        char *device,
        unsigned long position,
        format1_label_t *f1,
        format4_label_t *f4,
        format5_label_t *f5,
        format7_label_t *f7);

void vtoc_write_label (
        char *device,
        unsigned long position,
        format1_label_t *f1,
	format4_label_t *f4,
	format5_label_t *f5,
	format7_label_t *f7);


void vtoc_init_format1_label (
        char *volid,
        unsigned int blksize,
        extent_t *part_extent,
        format1_label_t *f1);


void vtoc_init_format4_label (
        format4_label_t *f4lbl,
	unsigned int usable_partitions,
	unsigned int cylinders,
	unsigned int tracks,
	unsigned int blocks,
	unsigned int blksize,
	__u16 dev_type);

void vtoc_update_format4_label (
	format4_label_t *f4,
	cchhb_t *highest_f1,
	__u16 unused_update);


void vtoc_init_format5_label (
	format5_label_t *f5);

void vtoc_update_format5_label_add (
	format5_label_t *f5,
	int verbose,
	int cyl,
	int trk,
	__u16 a, 
	__u16 b, 
	__u8 c);
 
void vtoc_update_format5_label_del (
	format5_label_t *f5,
	int verbose,
	int cyl,
	int trk,
	__u16 a, 
	__u16 b, 
	__u8 c);


void vtoc_init_format7_label (
	format7_label_t *f7);

void vtoc_update_format7_label_add (
	format7_label_t *f7,
	int verbose,
	__u32 a, 
	__u32 b);

void vtoc_update_format7_label_del (
	format7_label_t *f7, 
	int verbose,
	__u32 a, 
	__u32 b);


void vtoc_set_freespace(
	format4_label_t *f4,
	format5_label_t *f5,
	format7_label_t *f7,
	char ch,
	int verbose,
	__u32 start,
	__u32 stop,
	int cyl,
	int trk);












