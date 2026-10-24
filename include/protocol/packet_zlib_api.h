#pragma once

typedef int (*ZlibUncompressFunc)(unsigned char* dest, unsigned long* destLen,
                                  const unsigned char* source, unsigned long sourceLen);

// 只需要解压：全项目没有任何压缩发送路径（客户端发出的封包从不压缩），
// 原本导出的 GetZlibCompress/GetZlibCompress2 一直没有调用者，已移除。
// 将来若要发压缩包，把 compress/compress2 的导出加回来即可。
ZlibUncompressFunc GetZlibUncompress();
