#
# Makefile for the video capture/playback device drivers.
#

bttv-objs	:=	bttv-driver.o bttv-cards.o bttv-if.o \
			bttv-risc.o bttv-vbi.o
zoran-objs      :=	zr36120.o zr36120_i2c.o zr36120_mem.o

obj-$(CONFIG_VIDEO_DEV) += videodev.o v4l2-common.o v4l1-compat.o

obj-$(CONFIG_VIDEO_BT848) += bttv.o msp3400.o tvaudio.o \
	tda7432.o tda9875.o
obj-$(CONFIG_SOUND_TVMIXER) += tvmixer.o

obj-$(CONFIG_VIDEO_ZR36120) += zoran.o
obj-$(CONFIG_VIDEO_SAA5249) += saa5249.o
obj-$(CONFIG_VIDEO_CQCAM) += c-qcam.o
obj-$(CONFIG_VIDEO_BWQCAM) += bw-qcam.o
obj-$(CONFIG_VIDEO_W9966) += w9966.o
obj-$(CONFIG_VIDEO_ZORAN_BUZ) += saa7111.o saa7185.o
obj-$(CONFIG_VIDEO_ZORAN_DC10) += saa7110.o adv7175.o
obj-$(CONFIG_VIDEO_ZORAN_LML33) += bt819.o bt856.o
obj-$(CONFIG_VIDEO_ZORAN) += zr36067.o
obj-$(CONFIG_VIDEO_PMS) += pms.o
obj-$(CONFIG_VIDEO_PLANB) += planb.o
obj-$(CONFIG_VIDEO_VINO) += vino.o
obj-$(CONFIG_VIDEO_STRADIS) += stradis.o
obj-$(CONFIG_VIDEO_CPIA) += cpia.o
obj-$(CONFIG_VIDEO_CPIA_PP) += cpia_pp.o
obj-$(CONFIG_VIDEO_CPIA_USB) += cpia_usb.o
obj-$(CONFIG_VIDEO_MEYE) += meye.o
obj-$(CONFIG_VIDEO_SAA7134) += saa7134/
obj-$(CONFIG_VIDEO_MXB) += saa7111.o tuner.o tda9840.o tea6415c.o tea6420.o mxb.o
obj-$(CONFIG_VIDEO_DPC) += saa7111.o dpc7146.o
obj-$(CONFIG_TUNER_3036) += tuner-3036.o

obj-$(CONFIG_VIDEO_TUNER) += tuner.o tda9887.o
obj-$(CONFIG_VIDEO_BUF)   += video-buf.o

