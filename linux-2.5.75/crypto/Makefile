#
# Cryptographic API
#

autoload-crypto-$(CONFIG_KMOD) = autoload.o
proc-crypto-$(CONFIG_PROC_FS) = proc.o

obj-$(CONFIG_CRYPTO) += api.o cipher.o digest.o compress.o \
			$(autoload-crypto-y) $(proc-crypto-y)

obj-$(CONFIG_CRYPTO_HMAC) += hmac.o
obj-$(CONFIG_CRYPTO_NULL) += crypto_null.o
obj-$(CONFIG_CRYPTO_MD4) += md4.o
obj-$(CONFIG_CRYPTO_MD5) += md5.o
obj-$(CONFIG_CRYPTO_SHA1) += sha1.o
obj-$(CONFIG_CRYPTO_SHA256) += sha256.o
obj-$(CONFIG_CRYPTO_SHA512) += sha512.o
obj-$(CONFIG_CRYPTO_DES) += des.o
obj-$(CONFIG_CRYPTO_BLOWFISH) += blowfish.o
obj-$(CONFIG_CRYPTO_TWOFISH) += twofish.o
obj-$(CONFIG_CRYPTO_SERPENT) += serpent.o
obj-$(CONFIG_CRYPTO_AES) += aes.o
obj-$(CONFIG_CRYPTO_DEFLATE) += deflate.o

obj-$(CONFIG_CRYPTO_TEST) += tcrypt.o
