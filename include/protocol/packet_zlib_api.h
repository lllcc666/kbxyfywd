#pragma once

typedef int (*ZlibUncompressFunc)(unsigned char* dest, unsigned long* destLen,
                                  const unsigned char* source, unsigned long sourceLen);

typedef int (*ZlibCompressFunc)(unsigned char* dest, unsigned long* destLen,
                                const unsigned char* source, unsigned long sourceLen);

typedef int (*ZlibCompress2Func)(unsigned char* dest, unsigned long* destLen,
                                 const unsigned char* source, unsigned long sourceLen, int level);

ZlibUncompressFunc GetZlibUncompress();
ZlibCompressFunc GetZlibCompress();
ZlibCompress2Func GetZlibCompress2();
