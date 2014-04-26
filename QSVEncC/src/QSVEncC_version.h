﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _QSVENCC_VERSION_H_
#define _QSVENCC_VERSION_H_

#include "qsv_version.h"

#ifdef DEBUG
#define VER_DEBUG   VS_FF_DEBUG
#define VER_PRIVATE VS_FF_PRIVATEBUILD
#else
#define VER_DEBUG   0
#define VER_PRIVATE 0
#endif

#ifdef _M_IX86
#define QSVENC_FILENAME "QSVEncC (x86) - QuickSyncVideo Encoder (CUI)"
#else
#define QSVENC_FILENAME "QSVEncC (x64) - QuickSyncVideo Encoder (CUI)"
#endif

#define VER_STR_COMMENTS         "based on Intel Media SDK Sample"
#define VER_STR_COMPANYNAME      ""
#define VER_STR_FILEDESCRIPTION  QSVENC_FILENAME
#define VER_STR_INTERNALNAME     QSVENC_FILENAME
#define VER_STR_ORIGINALFILENAME "QSVEncC.exe"
#define VER_STR_LEGALCOPYRIGHT   "QSVEncC by rigaya"
#define VER_STR_PRODUCTNAME      QSVENC_FILENAME
#define VER_PRODUCTVERSION       VER_FILEVERSION
#define VER_STR_PRODUCTVERSION   VER_STR_FILEVERSION

#endif //_QSVENCC_VERSION_H_
