﻿//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2012 Intel Corporation. All Rights Reserved.
//

#include <tchar.h>
#include <windows.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <sstream>
#include <algorithm>

#include "pipeline_encode.h"
#include "vpy_reader.h"
#include "avs_reader.h"
#include "avi_reader.h"
#include "sysmem_allocator.h"

#if D3D_SURFACES_SUPPORT
#include "d3d_allocator.h"
#include "d3d11_allocator.h"

#include "d3d_device.h"
#include "d3d11_device.h"
#endif

#ifdef LIBVA_SUPPORT
#include "vaapi_allocator.h"
#include "vaapi_device.h"
#endif

//#include "../../sample_user_modules/plugin_api/plugin_loader.h"

//Library plugin UIDs
const mfxU8 HEVC_ENCODER_UID[] = {0x2f,0xca,0x99,0x74,0x9f,0xdb,0x49,0xae,0xb1,0x21,0xa5,0xb6,0x3e,0xf5,0x68,0xf7};

CEncTaskPool::CEncTaskPool()
{
	m_pTasks  = NULL;
	m_pmfxSession       = NULL;
	m_nTaskBufferStart  = 0;
	m_nPoolSize         = 0;
}

CEncTaskPool::~CEncTaskPool()
{
	Close();
}

mfxStatus CEncTaskPool::Init(MFXVideoSession* pmfxSession, CSmplBitstreamWriter* pWriter, mfxU32 nPoolSize, mfxU32 nBufferSize, CSmplBitstreamWriter *pOtherWriter)
{
	MSDK_CHECK_POINTER(pmfxSession, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(pWriter, MFX_ERR_NULL_PTR);

	MSDK_CHECK_ERROR(nPoolSize, 0, MFX_ERR_UNDEFINED_BEHAVIOR);
	MSDK_CHECK_ERROR(nBufferSize, 0, MFX_ERR_UNDEFINED_BEHAVIOR);

	// nPoolSize must be even in case of 2 output bitstreams
	if (pOtherWriter && (0 != nPoolSize % 2))
		return MFX_ERR_UNDEFINED_BEHAVIOR;

	m_pmfxSession = pmfxSession;
	m_nPoolSize = nPoolSize;

	m_pTasks = new sTask [m_nPoolSize];
	MSDK_CHECK_POINTER(m_pTasks, MFX_ERR_MEMORY_ALLOC);

	mfxStatus sts = MFX_ERR_NONE;

	if (pOtherWriter) // 2 bitstreams on output
	{
		for (mfxU32 i = 0; i < m_nPoolSize; i+=2)
		{
			sts = m_pTasks[i+0].Init(nBufferSize, pWriter);
			sts = m_pTasks[i+1].Init(nBufferSize, pOtherWriter);
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}
	}
	else
	{
		for (mfxU32 i = 0; i < m_nPoolSize; i++)
		{
			sts = m_pTasks[i].Init(nBufferSize, pWriter);
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}
	}

	return MFX_ERR_NONE;
}

mfxStatus CEncTaskPool::SynchronizeFirstTask()
{
	MSDK_CHECK_POINTER(m_pTasks, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_POINTER(m_pmfxSession, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts  = MFX_ERR_NONE;

	// non-null sync point indicates that task is in execution
	if (NULL != m_pTasks[m_nTaskBufferStart].EncSyncP)
	{
		sts = m_pmfxSession->SyncOperation(m_pTasks[m_nTaskBufferStart].EncSyncP, MSDK_WAIT_INTERVAL);

		if (MFX_ERR_NONE == sts)
		{
			sts = m_pTasks[m_nTaskBufferStart].WriteBitstream();
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

			sts = m_pTasks[m_nTaskBufferStart].Reset();
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

			// move task buffer start to the next executing task
			// the first transform frame to the right with non zero sync point
			for (mfxU32 i = 0; i < m_nPoolSize; i++)
			{
				m_nTaskBufferStart = (m_nTaskBufferStart + 1) % m_nPoolSize;
				if (NULL != m_pTasks[m_nTaskBufferStart].EncSyncP)
				{
					break;
				}
			}
		}
		else if (MFX_ERR_ABORTED == sts)
		{
			while (!m_pTasks[m_nTaskBufferStart].DependentVppTasks.empty())
			{
				// find out if the error occurred in a VPP task to perform recovery procedure if applicable
				sts = m_pmfxSession->SyncOperation(*m_pTasks[m_nTaskBufferStart].DependentVppTasks.begin(), 0);

				if (MFX_ERR_NONE == sts)
				{
					m_pTasks[m_nTaskBufferStart].DependentVppTasks.pop_front();
					sts = MFX_ERR_ABORTED; // save the status of the encode task
					continue; // go to next vpp task
				}
				else
				{
					break;
				}
			}
		}

		return sts;
	}
	else
	{
		return MFX_ERR_NOT_FOUND; // no tasks left in task buffer
	}
}

mfxU32 CEncTaskPool::GetFreeTaskIndex()
{
	mfxU32 off = 0;

	if (m_pTasks)
	{
		for (off = 0; off < m_nPoolSize; off++)
		{
			if (NULL == m_pTasks[(m_nTaskBufferStart + off) % m_nPoolSize].EncSyncP)
			{
				break;
			}
		}
	}

	if (off >= m_nPoolSize)
		return m_nPoolSize;

	return (m_nTaskBufferStart + off) % m_nPoolSize;
}

mfxStatus CEncTaskPool::GetFreeTask(sTask **ppTask)
{
	MSDK_CHECK_POINTER(ppTask, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pTasks, MFX_ERR_NOT_INITIALIZED);

	mfxU32 index = GetFreeTaskIndex();

	if (index >= m_nPoolSize)
	{
		return MFX_ERR_NOT_FOUND;
	}

	// return the address of the task
	*ppTask = &m_pTasks[index];

	return MFX_ERR_NONE;
}

void CEncTaskPool::Close()
{
	if (m_pTasks)
	{
		for (mfxU32 i = 0; i < m_nPoolSize; i++)
		{
			m_pTasks[i].Close();
		}
	}

	MSDK_SAFE_DELETE_ARRAY(m_pTasks);

	m_pmfxSession = NULL;
	m_nTaskBufferStart = 0;
	m_nPoolSize = 0;
}

sTask::sTask()
	: EncSyncP(0)
	, pWriter(NULL)
{
	MSDK_ZERO_MEMORY(mfxBS);
}

mfxStatus sTask::Init(mfxU32 nBufferSize, CSmplBitstreamWriter *pwriter)
{
	Close();

	pWriter = pwriter;

	mfxStatus sts = Reset();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitMfxBitstream(&mfxBS, nBufferSize);
	MSDK_CHECK_RESULT_SAFE(sts, MFX_ERR_NONE, sts, WipeMfxBitstream(&mfxBS));

	return sts;
}

mfxStatus sTask::Close()
{
	WipeMfxBitstream(&mfxBS);
	EncSyncP = 0;
	DependentVppTasks.clear();

	return MFX_ERR_NONE;
}

mfxStatus sTask::WriteBitstream()
{
	if (!pWriter)
		return MFX_ERR_NOT_INITIALIZED;

	return pWriter->WriteNextFrame(&mfxBS);
}

mfxStatus sTask::Reset()
{
	// mark sync point as free
	EncSyncP = NULL;

	// prepare bit stream
	mfxBS.DataOffset = 0;
	mfxBS.DataLength = 0;

	DependentVppTasks.clear();

	return MFX_ERR_NONE;
}

#if ENABLE_MVC_ENCODING
mfxStatus CEncodingPipeline::AllocAndInitMVCSeqDesc()
{
	// a simple example of mfxExtMVCSeqDesc structure filling
	// actually equal to the "Default dependency mode" - when the structure fields are left 0,
	// but we show how to properly allocate and fill the fields

	mfxU32 i;

	// mfxMVCViewDependency array
	m_MVCSeqDesc.NumView = m_nNumView;
	m_MVCSeqDesc.NumViewAlloc = m_nNumView;
	m_MVCSeqDesc.View = new mfxMVCViewDependency[m_MVCSeqDesc.NumViewAlloc];
	MSDK_CHECK_POINTER(m_MVCSeqDesc.View, MFX_ERR_MEMORY_ALLOC);
	for (i = 0; i < m_MVCSeqDesc.NumViewAlloc; ++i)
	{
		MSDK_ZERO_MEMORY(m_MVCSeqDesc.View[i]);
		m_MVCSeqDesc.View[i].ViewId = (mfxU16) i; // set view number as view id
	}

	// set up dependency for second view
	m_MVCSeqDesc.View[1].NumAnchorRefsL0 = 1;
	m_MVCSeqDesc.View[1].AnchorRefL0[0] = 0;     // ViewId 0 - base view

	m_MVCSeqDesc.View[1].NumNonAnchorRefsL0 = 1;
	m_MVCSeqDesc.View[1].NonAnchorRefL0[0] = 0;  // ViewId 0 - base view

	// viewId array
	m_MVCSeqDesc.NumViewId = m_nNumView;
	m_MVCSeqDesc.NumViewIdAlloc = m_nNumView;
	m_MVCSeqDesc.ViewId = new mfxU16[m_MVCSeqDesc.NumViewIdAlloc];
	MSDK_CHECK_POINTER(m_MVCSeqDesc.ViewId, MFX_ERR_MEMORY_ALLOC);
	for (i = 0; i < m_MVCSeqDesc.NumViewIdAlloc; ++i)
	{
		m_MVCSeqDesc.ViewId[i] = (mfxU16) i;
	}

	// create a single operation point containing all views
	m_MVCSeqDesc.NumOP = 1;
	m_MVCSeqDesc.NumOPAlloc = 1;
	m_MVCSeqDesc.OP = new mfxMVCOperationPoint[m_MVCSeqDesc.NumOPAlloc];
	MSDK_CHECK_POINTER(m_MVCSeqDesc.OP, MFX_ERR_MEMORY_ALLOC);
	for (i = 0; i < m_MVCSeqDesc.NumOPAlloc; ++i)
	{
		MSDK_ZERO_MEMORY(m_MVCSeqDesc.OP[i]);
		m_MVCSeqDesc.OP[i].NumViews = (mfxU16) m_nNumView;
		m_MVCSeqDesc.OP[i].NumTargetViews = (mfxU16) m_nNumView;
		m_MVCSeqDesc.OP[i].TargetViewId = m_MVCSeqDesc.ViewId; // points to mfxExtMVCSeqDesc::ViewId
	}

	return MFX_ERR_NONE;
}
#endif

//mfxStatus CEncodingPipeline::AllocAndInitVppDoNotUse()
//{
//	m_VppDoNotUse.NumAlg = 4;

//	m_VppDoNotUse.AlgList = new mfxU32 [m_VppDoNotUse.NumAlg];
//	MSDK_CHECK_POINTER(m_VppDoNotUse.AlgList,  MFX_ERR_MEMORY_ALLOC);

//	m_VppDoNotUse.AlgList[0] = MFX_EXTBUFF_VPP_DENOISE; // turn off denoising (on by default)
//	m_VppDoNotUse.AlgList[1] = MFX_EXTBUFF_VPP_SCENE_ANALYSIS; // turn off scene analysis (on by default)
//	m_VppDoNotUse.AlgList[2] = MFX_EXTBUFF_VPP_DETAIL; // turn off detail enhancement (on by default)
//	m_VppDoNotUse.AlgList[3] = MFX_EXTBUFF_VPP_PROCAMP; // turn off processing amplified (on by default)

//	return MFX_ERR_NONE;

//} // CEncodingPipeline::AllocAndInitVppDoNotUse()

#if ENABLE_MVC_ENCODING
void CEncodingPipeline::FreeMVCSeqDesc()
{
	MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.View);
	MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.ViewId);
	MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.OP);
}
#endif
//void CEncodingPipeline::FreeVppDoNotUse()
//{
//    MSDK_SAFE_DELETE(m_VppDoNotUse.AlgList);
//}

mfxStatus CEncodingPipeline::InitMfxEncParams(sInputParams *pInParams)
{
	//GOP長さが短いならVQPもシーンチェンジ検出も実行しない
	if (pInParams->nGOPLength != 0 && pInParams->nGOPLength < 4) {
		if (!pInParams->bforceGOPSettings) {
			PrintMes(_T("Scene change detection cannot be used with very short GOP length.\n"));
			pInParams->bforceGOPSettings = true;
		}
		if (pInParams->nEncMode == MFX_RATECONTROL_VQP)	{
			PrintMes(_T("VQP mode cannot be used with very short GOP length.\n"));
			PrintMes(_T("Switching to CQP mode.\n"));
			pInParams->nEncMode = MFX_RATECONTROL_CQP;
		}
	}
	//拡張設定
	if (!pInParams->bforceGOPSettings) {
		if (pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)
			&& (pInParams->vpp.nDeinterlace != MFX_DEINTERLACE_NORMAL && pInParams->vpp.nDeinterlace != MFX_DEINTERLACE_BOB)) {
			PrintMes(_T("Scene change detection cannot be used with interlaced output.\n"));
		} else {
			m_nExPrm |= MFX_PRM_EX_SCENE_CHANGE;
		}
	}
	if (pInParams->nEncMode == MFX_RATECONTROL_VQP)	{
		if (pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)
			&& (pInParams->vpp.nDeinterlace != MFX_DEINTERLACE_NORMAL && pInParams->vpp.nDeinterlace != MFX_DEINTERLACE_BOB)) {
			PrintMes(_T("VQP mode cannot be used with interlaced output.\n"));
			return MFX_ERR_INVALID_VIDEO_PARAM;
		}
		m_nExPrm |= MFX_PRM_EX_VQP;
	}
	//profileを守るための調整
	if (pInParams->CodecProfile == MFX_PROFILE_AVC_BASELINE) {
		pInParams->nBframes = 0;
		pInParams->bCAVLC = true;
	}
	if (pInParams->bCAVLC)
		pInParams->bRDO = false;

	//Lookaheadをチェック
	if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA) {
		bool lookahead_error = false;
		if (!check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
			PrintMes(_T("Lookahead mode is only supported by API v1.7 or later.\n"));
			lookahead_error = true;
		}
		if (!pInParams->bUseHWLib) {
			PrintMes(_T("Lookahead mode is only supported by Hardware encoder.\n"));
			lookahead_error = true;
		}
		if (!m_bHaswellOrLater) {
			PrintMes(_T("Lookahead mode is only supported by Haswell or later.\n"));
			lookahead_error = true;
		}
		if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF) && (pInParams->vpp.nDeinterlace == MFX_DEINTERLACE_NONE))) {
			PrintMes(_T("Lookahead mode does not support interlaced encoding.\n"));
			lookahead_error = true;
		}
		if (lookahead_error)
			return MFX_ERR_INVALID_VIDEO_PARAM;
	}

	//API v1.8のチェック (固定品質モード、ビデオ会議モード)
	if (   MFX_RATECONTROL_ICQ    == m_mfxEncParams.mfx.RateControlMethod
		|| MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod
		|| MFX_RATECONTROL_VCM    == m_mfxEncParams.mfx.RateControlMethod) {
		bool api1_8_check_error = false;
		if (!check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
			PrintMes(_T("%s mode is only supported by API v1.7 or later.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
			api1_8_check_error = true;
		}
		if (!m_bHaswellOrLater) {
			PrintMes(_T("%s mode is only supported by Haswell or later.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
			api1_8_check_error = true;
		}
		if (   MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod
			&& 0 != (pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))
			&& pInParams->vpp.nDeinterlace == MFX_DEINTERLACE_NONE) {
			PrintMes(_T("Lookahead mode does not support interlaced encoding.\n"));
			api1_8_check_error = true;
		}
		if (api1_8_check_error)
			return MFX_ERR_INVALID_VIDEO_PARAM;
	}

	m_mfxEncParams.mfx.CodecId                 = pInParams->CodecId;
	m_mfxEncParams.mfx.RateControlMethod       =(pInParams->nEncMode == MFX_RATECONTROL_VQP) ? MFX_RATECONTROL_CQP : pInParams->nEncMode;
	if (MFX_RATECONTROL_CQP == m_mfxEncParams.mfx.RateControlMethod) {
		//CQP
		m_mfxEncParams.mfx.QPI             = pInParams->nQPI;
		m_mfxEncParams.mfx.QPP             = pInParams->nQPP;
		m_mfxEncParams.mfx.QPB             = pInParams->nQPB;
	} else if (MFX_RATECONTROL_ICQ    == m_mfxEncParams.mfx.RateControlMethod
		    || MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod) {
		m_mfxEncParams.mfx.ICQQuality      = pInParams->nICQQuality;
		m_mfxEncParams.mfx.MaxKbps         = pInParams->nMaxBitrate;
	} else {
		m_mfxEncParams.mfx.TargetKbps      = pInParams->nBitRate; // in kbps
		if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR) {
			//AVBR
			//m_mfxEncParams.mfx.Accuracy        = pInParams->nAVBRAccuarcy;
			m_mfxEncParams.mfx.Accuracy        = 500;
			m_mfxEncParams.mfx.Convergence     = pInParams->nAVBRConvergence;
		} else {
			//CBR, VBR
			m_mfxEncParams.mfx.MaxKbps         = pInParams->nMaxBitrate;
		}
	}
	m_mfxEncParams.mfx.TargetUsage             = pInParams->nTargetUsage; // trade-off between quality and speed

	mfxU32 OutputFPSRate = pInParams->nFPSRate;
	mfxU32 OutputFPSScale = pInParams->nFPSScale;
	if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
		switch (pInParams->vpp.nDeinterlace) {
		case MFX_DEINTERLACE_IT:
			OutputFPSRate = OutputFPSRate * 4;
			OutputFPSScale = OutputFPSScale * 5;
			break;
		case MFX_DEINTERLACE_BOB:
			OutputFPSRate = OutputFPSRate * 2;
			break;
		default:
			break;
		}
	}
	mfxU32 gcd = GCD(OutputFPSRate, OutputFPSScale);
	OutputFPSRate /= gcd;
	OutputFPSScale /= gcd;
	if (pInParams->nGOPLength == 0) {
		pInParams->nGOPLength = (mfxU16)((OutputFPSRate + OutputFPSScale - 1) / OutputFPSScale) * 10;
	}
	m_mfxEncParams.mfx.FrameInfo.FrameRateExtN = OutputFPSRate;
	m_mfxEncParams.mfx.FrameInfo.FrameRateExtD = OutputFPSScale;
	m_mfxEncParams.mfx.EncodedOrder            = 0; // binary flag, 0 signals encoder to take frames in display order
	m_mfxEncParams.mfx.NumSlice                = pInParams->nSlices;

	m_mfxEncParams.mfx.NumRefFrame             = pInParams->nRef;
	m_mfxEncParams.mfx.CodecLevel              = pInParams->CodecLevel;
	m_mfxEncParams.mfx.CodecProfile            = pInParams->CodecProfile;
	m_mfxEncParams.mfx.GopOptFlag              = 0;
	m_mfxEncParams.mfx.GopOptFlag             |= (!pInParams->bopenGOP) ? MFX_GOP_CLOSED : NULL;
	//MFX_GOP_STRICTにより、インタレ保持時にフレームが壊れる場合があるため、無効とする
	//m_mfxEncParams.mfx.GopOptFlag             |= (pInParams->bforceGOPSettings) ? MFX_GOP_STRICT : NULL;

	m_mfxEncParams.mfx.GopPicSize              = pInParams->nGOPLength;
	m_mfxEncParams.mfx.GopRefDist              = (mfxU16)(clamp(pInParams->nBframes, -1, 16) + 1);

	// specify memory type
	m_mfxEncParams.IOPattern = (mfxU16)((pInParams->memType != SYSTEM_MEMORY) ? MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

	// frame info parameters
	m_mfxEncParams.mfx.FrameInfo.FourCC       = MFX_FOURCC_NV12;
	m_mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	m_mfxEncParams.mfx.FrameInfo.PicStruct    = (pInParams->vpp.nDeinterlace) ? MFX_PICSTRUCT_PROGRESSIVE : pInParams->nPicStruct;

	// set sar info
	mfxI32 m_iSAR[2] = { pInParams->nPAR[0], pInParams->nPAR[1] };
	adjust_sar(&m_iSAR[0], &m_iSAR[1], pInParams->nDstWidth, pInParams->nDstHeight);
	m_mfxEncParams.mfx.FrameInfo.AspectRatioW = (mfxU16)m_iSAR[0];
	m_mfxEncParams.mfx.FrameInfo.AspectRatioH = (mfxU16)m_iSAR[1];

	MSDK_ZERO_MEMORY(m_mfxCopt);
	m_mfxCopt.Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
	m_mfxCopt.Header.BufferSz = sizeof(mfxExtCodingOption);
	if (!pInParams->bUseHWLib) {
		//swライブラリ使用時のみ
		m_mfxCopt.InterPredBlockSize = pInParams->nInterPred;
		m_mfxCopt.IntraPredBlockSize = pInParams->nIntraPred;
		m_mfxCopt.MVSearchWindow     = pInParams->MVSearchWindow;
		m_mfxCopt.MVPrecision        = pInParams->nMVPrecision;
	}
	if (!pInParams->bUseHWLib || pInParams->CodecProfile == MFX_PROFILE_AVC_BASELINE) {
		//swライブラリ使用時かbaselineを指定した時
		m_mfxCopt.RateDistortionOpt  = (mfxU16)((pInParams->bRDO) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
		m_mfxCopt.CAVLC              = (mfxU16)((pInParams->bCAVLC) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
	}
	//m_mfxCopt.FramePicture = MFX_CODINGOPTION_ON;
	//m_mfxCopt.FieldOutput = MFX_CODINGOPTION_ON;
	//m_mfxCopt.VuiVclHrdParameters = MFX_CODINGOPTION_ON;
	//m_mfxCopt.VuiNalHrdParameters = MFX_CODINGOPTION_ON;
	m_mfxCopt.AUDelimiter = MFX_CODINGOPTION_OFF;
	m_mfxCopt.PicTimingSEI = MFX_CODINGOPTION_OFF;
	//m_mfxCopt.SingleSeiNalUnit = MFX_CODINGOPTION_OFF;

	//API v1.6の機能
	if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
		MSDK_ZERO_MEMORY(m_mfxCopt2);
		m_mfxCopt2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
		m_mfxCopt2.Header.BufferSz = sizeof(mfxExtCodingOption2);
		//IvyBridgeにおいて、このAPI v1.6の機能を使うと今のところうまく動かない
		//そこでAVX2フラグを確認して、Haswell以降でなら使用するようにする
		if (m_bHaswellOrLater) {
			if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
				m_mfxCopt2.AdaptiveI   = (mfxU16)((pInParams->bAdaptiveI) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
				m_mfxCopt2.AdaptiveB   = (mfxU16)((pInParams->bAdaptiveB) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
				m_mfxCopt2.BRefType    = (mfxU16)((pInParams->bBPyramid)  ? MFX_B_REF_PYRAMID   : MFX_B_REF_UNKNOWN);
				m_mfxCopt2.LookAheadDS = pInParams->nLookaheadDS;
			}
			if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
				m_mfxCopt2.LookAheadDepth = (pInParams->nLookaheadDepth == 0) ? pInParams->nLookaheadDepth : clamp(pInParams->nLookaheadDepth, QSV_LOOKAHEAD_DEPTH_MIN, QSV_LOOKAHEAD_DEPTH_MAX);
				m_mfxCopt2.Trellis = pInParams->nTrellis;
			}
			if (pInParams->bMBBRC) {
				m_mfxCopt2.MBBRC = MFX_CODINGOPTION_ON;
			}

			if (pInParams->bExtBRC
				&& (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR
				 || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR
				 || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR
				 || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_VCM
				 || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA)) {
				m_mfxCopt2.ExtBRC = MFX_CODINGOPTION_ON;
			}
			m_EncExtParams.push_back((mfxExtBuffer *)&m_mfxCopt2);
		} else if (pInParams->bMBBRC || pInParams->bExtBRC) {
			PrintMes(_T("API v1.6 feature is currently limited to Haswell CPUs.\n"));
		}
	}

	//Bluray互換出力
	if (pInParams->nBluray) {
		if (   m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_CBR
			&& m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_VBR
			&& m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_LA) {
				if (pInParams->nBluray == 1) {
					PrintMes(_T("")
						_T("Current encode mode (%s) is not preferred for Bluray encoding,\n")
						_T("since it cannot set Max Bitrate.\n")
						_T("Please consider using Lookahead/VBR/CBR mode for Bluray encoding.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
					return MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;
				} else {
					//pInParams->nBluray == 2 -> force Bluray
					PrintMes(_T("")
						_T("Current encode mode (%s) is not preferred for Bluray encoding,\n")
						_T("since it cannot set Max Bitrate.\n")
						_T("This output might not be able to be played on a Bluray Player.\n")
						_T("Please consider using Lookahead/VBR/CBR mode for Bluray encoding.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
				}
		}
		if (   m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR
			|| m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR
			|| m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA) {
				m_mfxEncParams.mfx.MaxKbps    = min(m_mfxEncParams.mfx.MaxKbps, 40000);
				m_mfxEncParams.mfx.TargetKbps = min(m_mfxEncParams.mfx.TargetKbps, m_mfxEncParams.mfx.MaxKbps);
				m_mfxEncParams.mfx.BufferSizeInKB = m_mfxEncParams.mfx.MaxKbps / 8;
				m_mfxEncParams.mfx.InitialDelayInKB = m_mfxEncParams.mfx.BufferSizeInKB / 2;
		} else {
			m_mfxEncParams.mfx.BufferSizeInKB = 25000 / 8;
		}
		m_mfxEncParams.mfx.CodecLevel = (m_mfxEncParams.mfx.CodecLevel == 0) ? MFX_LEVEL_AVC_41 : (min(m_mfxEncParams.mfx.CodecLevel, MFX_LEVEL_AVC_41));
		m_mfxEncParams.mfx.NumSlice   = max(m_mfxEncParams.mfx.NumSlice, 4);
		m_mfxEncParams.mfx.GopOptFlag &= (~MFX_GOP_STRICT);
		m_mfxEncParams.mfx.GopRefDist = min(m_mfxEncParams.mfx.GopRefDist, 3+1);
		m_mfxEncParams.mfx.GopPicSize = (int)(min(m_mfxEncParams.mfx.GopPicSize, 30) / m_mfxEncParams.mfx.GopRefDist) * m_mfxEncParams.mfx.GopRefDist;
		m_mfxEncParams.mfx.NumRefFrame = min(m_mfxEncParams.mfx.NumRefFrame, 6);
		m_mfxCopt.MaxDecFrameBuffering = m_mfxEncParams.mfx.NumRefFrame;
		m_mfxCopt.VuiNalHrdParameters = MFX_CODINGOPTION_ON;
		m_mfxCopt.VuiVclHrdParameters = MFX_CODINGOPTION_ON;
		m_mfxCopt.AUDelimiter  = MFX_CODINGOPTION_ON;
		m_mfxCopt.PicTimingSEI = MFX_CODINGOPTION_ON;
		m_mfxCopt.ResetRefList = MFX_CODINGOPTION_ON;
		m_nExPrm &= (~MFX_PRM_EX_SCENE_CHANGE);
		//m_mfxCopt.EndOfSequence = MFX_CODINGOPTION_ON; //hwモードでは効果なし 0x00, 0x00, 0x01, 0x0a
		//m_mfxCopt.EndOfStream   = MFX_CODINGOPTION_ON; //hwモードでは効果なし 0x00, 0x00, 0x01, 0x0b
	}

	m_EncExtParams.push_back((mfxExtBuffer *)&m_mfxCopt);

	//m_mfxEncParams.mfx.TimeStampCalc = (mfxU16)((pInParams->vpp.nDeinterlace == MFX_DEINTERLACE_IT) ? MFX_TIMESTAMPCALC_TELECINE : MFX_TIMESTAMPCALC_UNKNOWN);
	//m_mfxEncParams.mfx.ExtendedPicStruct = pInParams->nPicStruct;

	if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3) &&
		(pInParams->VideoFormat != list_videoformat[0].value ||
		 pInParams->ColorPrim   != list_colorprim[0].value ||
		 pInParams->Transfer    != list_transfer[0].value ||
		 pInParams->ColorMatrix != list_colormatrix[0].value ||
		 pInParams->bFullrange
		) ) {
#define GET_COLOR_PRM(v, list) (mfxU16)((v == MFX_COLOR_VALUE_AUTO) ? ((pInParams->nDstHeight >= HD_HEIGHT_THRESHOLD) ? list[HD_INDEX].value : list[SD_INDEX].value) : v)
			//色設定 (for API v1.3)
			MSDK_ZERO_MEMORY(m_mfxVSI);
			m_mfxVSI.Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
			m_mfxVSI.Header.BufferSz = sizeof(mfxExtVideoSignalInfo);
			m_mfxVSI.ColourDescriptionPresent = 1; //"1"と設定しないと正しく反映されない
			m_mfxVSI.VideoFormat              = pInParams->VideoFormat;
			m_mfxVSI.VideoFullRange           = pInParams->bFullrange != 0;
			m_mfxVSI.ColourPrimaries          = GET_COLOR_PRM(pInParams->ColorPrim,   list_colorprim);
			m_mfxVSI.TransferCharacteristics  = GET_COLOR_PRM(pInParams->Transfer,    list_transfer);
			m_mfxVSI.MatrixCoefficients       = GET_COLOR_PRM(pInParams->ColorMatrix, list_colormatrix);
#undef GET_COLOR_PRM
			m_EncExtParams.push_back((mfxExtBuffer *)&m_mfxVSI);
	}

	//シーンチェンジ検出をこちらで行う場合は、GOP長を最大に設定する
	if (m_nExPrm & MFX_PRM_EX_SCENE_CHANGE)
		m_mfxEncParams.mfx.GopPicSize = USHRT_MAX;

	// set frame size and crops
	// width must be a multiple of 16
	// height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
	m_mfxEncParams.mfx.FrameInfo.Width  = MSDK_ALIGN16(pInParams->nDstWidth);
	m_mfxEncParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxEncParams.mfx.FrameInfo.PicStruct)?
		MSDK_ALIGN16(pInParams->nDstHeight) : MSDK_ALIGN32(pInParams->nDstHeight);

	m_mfxEncParams.mfx.FrameInfo.CropX = 0;
	m_mfxEncParams.mfx.FrameInfo.CropY = 0;
	m_mfxEncParams.mfx.FrameInfo.CropW = pInParams->nDstWidth;
	m_mfxEncParams.mfx.FrameInfo.CropH = pInParams->nDstHeight;
#if ENABLE_MVC_ENCODING
	// we don't specify profile and level and let the encoder choose those basing on parameters
	// we must specify profile only for MVC codec
	if (MVC_ENABLED & m_MVCflags)
		m_mfxEncParams.mfx.CodecProfile = MFX_PROFILE_AVC_STEREO_HIGH;

	// configure and attach external parameters
	if (MVC_ENABLED & pInParams->MVC_flags)
		m_EncExtParams.push_back((mfxExtBuffer *)&m_MVCSeqDesc);

	if (MVC_VIEWOUTPUT & pInParams->MVC_flags)
	{
		// ViewOuput option requested
		m_CodingOption.ViewOutput = MFX_CODINGOPTION_ON;
		m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption);
	}
#endif

	// JPEG encoder settings overlap with other encoders settings in mfxInfoMFX structure
	if (MFX_CODEC_JPEG == pInParams->CodecId)
	{
		m_mfxEncParams.mfx.Interleaved = 1;
		m_mfxEncParams.mfx.Quality = pInParams->nQuality;
		m_mfxEncParams.mfx.RestartInterval = 0;
		MSDK_ZERO_MEMORY(m_mfxEncParams.mfx.reserved5);
	}

	if (!m_EncExtParams.empty())
	{
		m_mfxEncParams.ExtParam = &m_EncExtParams[0]; // vector is stored linearly in memory
		m_mfxEncParams.NumExtParam = (mfxU16)m_EncExtParams.size();
	}

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::InitMfxVppParams(sInputParams *pInParams)
{
	MSDK_CHECK_POINTER(pInParams,  MFX_ERR_NULL_PTR);

	// specify memory type
	if (pInParams->memType != SYSTEM_MEMORY)
		m_mfxVppParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
	else
		m_mfxVppParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;


	m_mfxVppParams.vpp.In.PicStruct = pInParams->nPicStruct;
	m_mfxVppParams.vpp.In.FrameRateExtN = pInParams->nFPSRate;
	m_mfxVppParams.vpp.In.FrameRateExtD = pInParams->nFPSScale;

	mfxFrameInfo inputFrameInfo = { 0 };
	m_pFileReader->GetInputFrameInfo(&inputFrameInfo);
	if (inputFrameInfo.FourCC == 0 || inputFrameInfo.FourCC == MFX_FOURCC_NV12) {
		// input frame info
		m_mfxVppParams.vpp.In.FourCC       = (pInParams->vpp.bColorFmtConvertion) ? MFX_FOURCC_YUY2 : MFX_FOURCC_NV12;
		m_mfxVppParams.vpp.In.ChromaFormat = (mfxU16)((pInParams->vpp.bColorFmtConvertion) ? MFX_CHROMAFORMAT_YUV422 : MFX_CHROMAFORMAT_YUV420);

		// width must be a multiple of 16
		// height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
		m_mfxVppParams.vpp.In.Width     = MSDK_ALIGN16(pInParams->nWidth);
		m_mfxVppParams.vpp.In.Height    = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.In.PicStruct)?
			MSDK_ALIGN16(pInParams->nHeight) : MSDK_ALIGN32(pInParams->nHeight);
	} else {
		m_mfxVppParams.vpp.In.FourCC       = inputFrameInfo.FourCC;
		m_mfxVppParams.vpp.In.ChromaFormat = inputFrameInfo.ChromaFormat;

		// width must be a multiple of 16
		// height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
		m_mfxVppParams.vpp.In.Width     = MSDK_ALIGN16(inputFrameInfo.CropW);
		m_mfxVppParams.vpp.In.Height    = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.In.PicStruct)?
			MSDK_ALIGN16(inputFrameInfo.CropH) : MSDK_ALIGN32(inputFrameInfo.CropH);
	}

	// set crops in input mfxFrameInfo for correct work of file reader
	// VPP itself ignores crops at initialization
	m_mfxVppParams.vpp.In.CropW = pInParams->nWidth;
	m_mfxVppParams.vpp.In.CropH = pInParams->nHeight;

	// fill output frame info
	memcpy(&m_mfxVppParams.vpp.Out, &m_mfxVppParams.vpp.In, sizeof(mfxFrameInfo));

	// only resizing is supported
	m_mfxVppParams.vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	m_mfxVppParams.vpp.Out.FourCC = MFX_FOURCC_NV12;
	m_mfxVppParams.vpp.Out.PicStruct = (pInParams->vpp.nDeinterlace) ? MFX_PICSTRUCT_PROGRESSIVE : pInParams->nPicStruct;
	if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
		switch (pInParams->vpp.nDeinterlace) {
		case MFX_DEINTERLACE_NORMAL:
			VppExtMes += _T("Deinterlace\n");
			m_nExPrm |= MFX_PRM_EX_DEINT_NORMAL;
			break;
		case MFX_DEINTERLACE_IT:
			m_mfxVppParams.vpp.Out.FrameRateExtN = (m_mfxVppParams.vpp.Out.FrameRateExtN * 4) / 5;
			VppExtMes += _T("Deinterlace (Inverse Telecine)\n");
			break;
		case MFX_DEINTERLACE_BOB:
			m_mfxVppParams.vpp.Out.FrameRateExtN = m_mfxVppParams.vpp.Out.FrameRateExtN * 2;
			m_nExPrm |= MFX_PRM_EX_DEINT_BOB;
			VppExtMes += _T("Deinterlace (Double)\n");
			break;
		case MFX_DEINTERLACE_NONE:
		default:
			break;
		}
	}
	m_mfxVppParams.vpp.Out.Width = MSDK_ALIGN16(pInParams->nDstWidth);
	m_mfxVppParams.vpp.Out.Height = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.Out.PicStruct)?
		MSDK_ALIGN16(pInParams->nDstHeight) : MSDK_ALIGN32(pInParams->nDstHeight);

	// configure and attach external parameters
	//AllocAndInitVppDoNotUse();
	//m_VppExtParams.push_back((mfxExtBuffer *)&m_VppDoNotUse);
#if ENABLE_MVC_ENCODING
	if (pInParams->bIsMVC)
		m_VppExtParams.push_back((mfxExtBuffer *)&m_MVCSeqDesc);
#endif
	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::CreateVppExtBuffers(sInputParams *pParams)
{
	m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_PROCAMP);

	if (pParams->vpp.bUseDenoise) {
		MSDK_ZERO_MEMORY(m_ExtDenoise);
		m_ExtDenoise.Header.BufferId = MFX_EXTBUFF_VPP_DENOISE;
		m_ExtDenoise.Header.BufferSz = sizeof(mfxExtVPPDenoise);
		m_ExtDenoise.DenoiseFactor  = pParams->vpp.nDenoise;
		m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDenoise);

		TStringStream stream;
		stream << _T("Denoise, strength ") << m_ExtDenoise.DenoiseFactor << _T("\n");
		VppExtMes += stream.str();
		m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
	} else {
		m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
	}

	if (pParams->vpp.bUseDetailEnhance) {
		MSDK_ZERO_MEMORY(m_ExtDetail);
		m_ExtDetail.Header.BufferId = MFX_EXTBUFF_VPP_DETAIL;
		m_ExtDetail.Header.BufferSz = sizeof(mfxExtVPPDetail);
		m_ExtDetail.DetailFactor = pParams->vpp.nDetailEnhance;
		m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDetail);

		TStringStream stream;
		stream << _T("Detail Enhancer, strength ") << m_ExtDetail.DetailFactor << _T("\n");
		VppExtMes += stream.str();
		m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
	} else {
		m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
	}

	m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_SCENE_ANALYSIS);

	if (   check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3)
		&& (pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
			switch (pParams->vpp.nDeinterlace) {
			case MFX_DEINTERLACE_IT:
			case MFX_DEINTERLACE_BOB:
				MSDK_ZERO_MEMORY(m_ExtFrameRateConv);
				m_ExtFrameRateConv.Header.BufferId = MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION;
				m_ExtFrameRateConv.Header.BufferSz = sizeof(m_ExtFrameRateConv);
				m_ExtFrameRateConv.Algorithm = MFX_FRCALGM_DISTRIBUTED_TIMESTAMP;

				m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
				break;
			default:
				break;
			}
	}

	if (m_VppDoUseList.size()) {
		MSDK_ZERO_MEMORY(m_ExtDoUse);
		m_ExtDoUse.Header.BufferId = MFX_EXTBUFF_VPP_DOUSE;
		m_ExtDoUse.Header.BufferSz = sizeof(mfxExtVPPDoUse);
		m_ExtDoUse.NumAlg = (mfxU32)m_VppDoUseList.size();
		m_ExtDoUse.AlgList = &m_VppDoUseList[0];

		m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtDoUse);
	}

	if (m_VppDoNotUseList.size()) {
		// configure vpp DoNotUse hint
		MSDK_ZERO_MEMORY(m_ExtDoNotUse);
		m_ExtDoNotUse.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
		m_ExtDoNotUse.Header.BufferSz = sizeof(mfxExtVPPDoNotUse);
		m_ExtDoNotUse.NumAlg = (mfxU32)m_VppDoNotUseList.size();
		m_ExtDoNotUse.AlgList = &m_VppDoNotUseList[0];

		m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtDoNotUse);
	}

	m_mfxVppParams.ExtParam = &m_VppExtParams[0]; // vector is stored linearly in memory
	m_mfxVppParams.NumExtParam = (mfxU16)m_VppExtParams.size();

	return MFX_ERR_NONE;
}

//void CEncodingPipeline::DeleteVppExtBuffers()
//{
//	//free external buffers
//	if (m_ppVppExtBuffers)
//	{
//		for (mfxU8 i = 0; i < m_nNumVppExtBuffers; i++)
//		{
//			mfxExtVPPDoNotUse* pExtDoNotUse = (mfxExtVPPDoNotUse* )(m_ppVppExtBuffers[i]);
//			SAFE_DELETE_ARRAY(pExtDoNotUse->AlgList);
//			SAFE_DELETE(m_ppVppExtBuffers[i]);
//		}
//	}
//
//	SAFE_DELETE_ARRAY(m_ppVppExtBuffers);
//}

mfxStatus CEncodingPipeline::CreateHWDevice()
{
	mfxStatus sts = MFX_ERR_NONE;
#if D3D_SURFACES_SUPPORT
	POINT point = {0, 0};
	HWND window = WindowFromPoint(point);
	m_hwdev = NULL;

	if (m_memType) {
#if MFX_D3D11_SUPPORT
		if (m_memType == D3D11_MEMORY
			&& NULL != (m_hwdev = new CD3D11Device())) {
			m_memType = D3D11_MEMORY;

			sts = m_hwdev->Init(
				window,
				0,
				MSDKAdapter::GetNumber(m_mfxSession));
			if (sts != MFX_ERR_NONE) {
				m_hwdev->Close();
				delete m_hwdev;
				m_hwdev = NULL;
			}
		}
#endif // #if MFX_D3D11_SUPPORT
		if (m_hwdev == NULL && NULL != (m_hwdev = new CD3D9Device())) {
			//もし、d3d11要求で失敗したら自動的にd3d9に切り替える
			//sessionごと切り替える必要がある
			if (m_memType != D3D9_MEMORY) {
				InitSession(true, D3D9_MEMORY);
				m_memType = m_memType;
			}

			sts = m_hwdev->Init(
				window,
				0,
				MSDKAdapter::GetNumber(m_mfxSession));
		}
	}
	
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	
#elif LIBVA_SUPPORT
    m_hwdev = CreateVAAPIDevice();
    if (NULL == m_hwdev)
    {
        return MFX_ERR_MEMORY_ALLOC;
    }
    sts = m_hwdev->Init(NULL, 0, MSDKAdapter::GetNumber(m_mfxSession));
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
#endif
    return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::ResetDevice()
{
	if (m_memType & (D3D9_MEMORY | D3D11_MEMORY))
	{
		return m_hwdev->Reset();
	}
	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::AllocFrames()
{
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;
	mfxFrameAllocRequest EncRequest;
	mfxFrameAllocRequest VppRequest[2];

	mfxU16 nEncSurfNum = 0; // number of surfaces for encoder
	mfxU16 nVppSurfNum = 0; // number of surfaces for vpp

	MSDK_ZERO_MEMORY(EncRequest);
	MSDK_ZERO_MEMORY(VppRequest[0]);
	MSDK_ZERO_MEMORY(VppRequest[1]);

	// Calculate the number of surfaces for components.
	// QueryIOSurf functions tell how many surfaces are required to produce at least 1 output.
	// To achieve better performance we provide extra surfaces.
	// 1 extra surface at input allows to get 1 extra output.

	sts = m_pmfxENC->QueryIOSurf(&m_mfxEncParams, &EncRequest);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (m_pmfxVPP)
	{
		// VppRequest[0] for input frames request, VppRequest[1] for output frames request
		sts = m_pmfxVPP->QueryIOSurf(&m_mfxVppParams, VppRequest);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	mfxU16 inputBufNum = m_EncThread.m_nFrameBuffer;
	// The number of surfaces shared by vpp output and encode input.
	// When surfaces are shared 1 surface at first component output contains output frame that goes to next component input
	nEncSurfNum = EncRequest.NumFrameSuggested + MSDK_MAX(VppRequest[1].NumFrameSuggested, 1) - 1 + (m_nAsyncDepth - 1) + ((m_pmfxVPP) ? 0 : inputBufNum);

	// The number of surfaces for vpp input - so that vpp can work at async depth = m_nAsyncDepth
	nVppSurfNum = VppRequest[0].NumFrameSuggested + (m_nAsyncDepth - 1) + ((m_pmfxVPP) ? inputBufNum : 0);

	// prepare allocation requests
	EncRequest.NumFrameMin = nEncSurfNum;
	EncRequest.NumFrameSuggested = nEncSurfNum;
	memcpy(&(EncRequest.Info), &(m_mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
	if (m_pmfxVPP)
	{
		EncRequest.Type |= MFX_MEMTYPE_FROM_VPPOUT; // surfaces are shared between vpp output and encode input
	}

	// alloc frames for encoder
	sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, &EncRequest, &m_EncResponse);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// alloc frames for vpp if vpp is enabled
	if (m_pmfxVPP)
	{
		VppRequest[0].NumFrameMin = nVppSurfNum;
		VppRequest[0].NumFrameSuggested = nVppSurfNum;
		memcpy(&(VppRequest[0].Info), &(m_mfxVppParams.mfx.FrameInfo), sizeof(mfxFrameInfo));

		sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, &(VppRequest[0]), &m_VppResponse);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	// prepare mfxFrameSurface1 array for encoder
	m_pEncSurfaces = new mfxFrameSurface1 [m_EncResponse.NumFrameActual];
	MSDK_CHECK_POINTER(m_pEncSurfaces, MFX_ERR_MEMORY_ALLOC);

	for (int i = 0; i < m_EncResponse.NumFrameActual; i++)
	{
		memset(&(m_pEncSurfaces[i]), 0, sizeof(mfxFrameSurface1));
		memcpy(&(m_pEncSurfaces[i].Info), &(m_mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));

		if (m_bExternalAlloc)
		{
			m_pEncSurfaces[i].Data.MemId = m_EncResponse.mids[i];
		}
		else
		{
			// get YUV pointers
			sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_EncResponse.mids[i], &(m_pEncSurfaces[i].Data));
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}
	}

	// prepare mfxFrameSurface1 array for vpp if vpp is enabled
	if (m_pmfxVPP)
	{
		m_pVppSurfaces = new mfxFrameSurface1 [m_VppResponse.NumFrameActual];
		MSDK_CHECK_POINTER(m_pVppSurfaces, MFX_ERR_MEMORY_ALLOC);

		for (int i = 0; i < m_VppResponse.NumFrameActual; i++)
		{
			memset(&(m_pVppSurfaces[i]), 0, sizeof(mfxFrameSurface1));
			memcpy(&(m_pVppSurfaces[i].Info), &(m_mfxVppParams.mfx.FrameInfo), sizeof(mfxFrameInfo));

			if (m_bExternalAlloc)
			{
				m_pVppSurfaces[i].Data.MemId = m_VppResponse.mids[i];
			}
			else
			{
				sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_VppResponse.mids[i], &(m_pVppSurfaces[i].Data));
				MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
			}
		}
	}

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::CreateAllocator()
{
    mfxStatus sts = MFX_ERR_NONE;

    if (D3D9_MEMORY == m_memType || D3D11_MEMORY == m_memType)
    {
#if D3D_SURFACES_SUPPORT
        sts = CreateHWDevice();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        mfxHDL hdl = NULL;
        mfxHandleType hdl_t =
        #if MFX_D3D11_SUPPORT
            D3D11_MEMORY == m_memType ? MFX_HANDLE_D3D11_DEVICE :
        #endif // #if MFX_D3D11_SUPPORT
            MFX_HANDLE_D3D9_DEVICE_MANAGER;

        sts = m_hwdev->GetHandle(hdl_t, &hdl);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        
        // handle is needed for HW library only
        mfxIMPL impl = 0;
        m_mfxSession.QueryIMPL(&impl);
        if (impl != MFX_IMPL_SOFTWARE)
        {
            sts = m_mfxSession.SetHandle(hdl_t, hdl);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts); 
        }

        // create D3D allocator
#if MFX_D3D11_SUPPORT
        if (D3D11_MEMORY == m_memType)
        {
            m_pMFXAllocator = new D3D11FrameAllocator;
            MSDK_CHECK_POINTER(m_pMFXAllocator, MFX_ERR_MEMORY_ALLOC);

            D3D11AllocatorParams *pd3dAllocParams = new D3D11AllocatorParams;
            MSDK_CHECK_POINTER(pd3dAllocParams, MFX_ERR_MEMORY_ALLOC);
            pd3dAllocParams->pDevice = reinterpret_cast<ID3D11Device *>(hdl);

            m_pmfxAllocatorParams = pd3dAllocParams;
        }
        else
#endif // #if MFX_D3D11_SUPPORT
        {
            m_pMFXAllocator = new D3DFrameAllocator;
            MSDK_CHECK_POINTER(m_pMFXAllocator, MFX_ERR_MEMORY_ALLOC);

            D3DAllocatorParams *pd3dAllocParams = new D3DAllocatorParams;
            MSDK_CHECK_POINTER(pd3dAllocParams, MFX_ERR_MEMORY_ALLOC);
            pd3dAllocParams->pManager = reinterpret_cast<IDirect3DDeviceManager9 *>(hdl);

            m_pmfxAllocatorParams = pd3dAllocParams;
        }

        /* In case of video memory we must provide MediaSDK with external allocator
        thus we demonstrate "external allocator" usage model.
        Call SetAllocator to pass allocator to Media SDK */
        sts = m_mfxSession.SetFrameAllocator(m_pMFXAllocator);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        m_bExternalAlloc = true;
#endif
#ifdef LIBVA_SUPPORT
        sts = CreateHWDevice();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        /* It's possible to skip failed result here and switch to SW implementation,
        but we don't process this way */

        mfxHDL hdl = NULL;
        sts = m_hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, &hdl);
        // provide device manager to MediaSDK
        sts = m_mfxSession.SetHandle(MFX_HANDLE_VA_DISPLAY, hdl);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        // create VAAPI allocator
        m_pMFXAllocator = new vaapiFrameAllocator;
        MSDK_CHECK_POINTER(m_pMFXAllocator, MFX_ERR_MEMORY_ALLOC);

        vaapiAllocatorParams *p_vaapiAllocParams = new vaapiAllocatorParams;
        MSDK_CHECK_POINTER(p_vaapiAllocParams, MFX_ERR_MEMORY_ALLOC);

        p_vaapiAllocParams->m_dpy = (VADisplay)hdl;
        m_pmfxAllocatorParams = p_vaapiAllocParams;

        /* In case of video memory we must provide MediaSDK with external allocator 
        thus we demonstrate "external allocator" usage model.
        Call SetAllocator to pass allocator to mediasdk */
        sts = m_mfxSession.SetFrameAllocator(m_pMFXAllocator);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        m_bExternalAlloc = true;
#endif
    }
    else
    {
#ifdef LIBVA_SUPPORT
        //in case of system memory allocator we also have to pass MFX_HANDLE_VA_DISPLAY to HW library
        mfxIMPL impl;
        m_mfxSession.QueryIMPL(&impl);

        if(MFX_IMPL_HARDWARE == MFX_IMPL_BASETYPE(impl))
        {
            sts = CreateHWDevice();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            mfxHDL hdl = NULL;
            sts = m_hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, &hdl);
            // provide device manager to MediaSDK
            sts = m_mfxSession.SetHandle(MFX_HANDLE_VA_DISPLAY, hdl);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
#endif

        // create system memory allocator
        m_pMFXAllocator = new SysMemFrameAllocator;
        MSDK_CHECK_POINTER(m_pMFXAllocator, MFX_ERR_MEMORY_ALLOC);

        /* In case of system memory we demonstrate "no external allocator" usage model.
        We don't call SetAllocator, Media SDK uses internal allocator.
        We use system memory allocator simply as a memory manager for application*/
    }

    // initialize memory allocator
    sts = m_pMFXAllocator->Init(m_pmfxAllocatorParams);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    return MFX_ERR_NONE;
}

void CEncodingPipeline::DeleteFrames()
{
	// delete surfaces array
	MSDK_SAFE_DELETE_ARRAY(m_pEncSurfaces);
	MSDK_SAFE_DELETE_ARRAY(m_pVppSurfaces);

	// delete frames
	if (m_pMFXAllocator)
	{
		m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_EncResponse);
		m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_VppResponse);
	}
}

void CEncodingPipeline::DeleteHWDevice()
{
#if D3D_SURFACES_SUPPORT
	MSDK_SAFE_DELETE(m_hwdev);
#endif
}

void CEncodingPipeline::DeleteAllocator()
{
	// delete allocator
	MSDK_SAFE_DELETE(m_pMFXAllocator);
	MSDK_SAFE_DELETE(m_pmfxAllocatorParams);

	DeleteHWDevice();
}

CEncodingPipeline::CEncodingPipeline()
{
	m_pmfxENC = NULL;
	m_pmfxVPP = NULL;
	m_pMFXAllocator = NULL;
	m_pmfxAllocatorParams = NULL;
	m_memType = SYSTEM_MEMORY;
	m_bExternalAlloc = false;
	m_pEncSurfaces = NULL;
	m_pVppSurfaces = NULL;
	m_nAsyncDepth = 0;
	m_nExPrm = 0x00;

	m_bHaswellOrLater = false;
	m_pAbortByUser = NULL;

	m_pEncSatusInfo = NULL;
	m_pFileWriter = NULL;
	m_pFileReader = NULL;

	m_pStrLog = NULL;

	//MSDK_ZERO_MEMORY(m_VppDoNotUse);

	MSDK_ZERO_MEMORY(m_mfxVSI);
	MSDK_ZERO_MEMORY(m_mfxCopt);
#if ENABLE_MVC_ENCODING
	m_bIsMVC = false;
	m_MVCflags = MVC_DISABLED;
	m_nNumView = 0;
	MSDK_ZERO_MEMORY(m_MVCSeqDesc);
	m_MVCSeqDesc.Header.BufferId = MFX_EXTBUFF_MVC_SEQ_DESC;
	m_MVCSeqDesc.Header.BufferSz = sizeof(mfxExtMVCSeqDesc);
#endif
	//m_VppDoNotUse.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
	//m_VppDoNotUse.Header.BufferSz = sizeof(mfxExtVPPDoNotUse);

#if D3D_SURFACES_SUPPORT
	m_hwdev = NULL;
#endif

	MSDK_ZERO_MEMORY(m_mfxEncParams);
	MSDK_ZERO_MEMORY(m_mfxVppParams);

	MSDK_ZERO_MEMORY(m_ExtDoUse);
	MSDK_ZERO_MEMORY(m_ExtDoNotUse);
	MSDK_ZERO_MEMORY(m_ExtDenoise);
	MSDK_ZERO_MEMORY(m_ExtDetail);

	MSDK_ZERO_MEMORY(m_EncResponse);
	MSDK_ZERO_MEMORY(m_VppResponse);
}

CEncodingPipeline::~CEncodingPipeline()
{
	Close();
}

void CEncodingPipeline::SetAbortFlagPointer(bool *abortFlag) {
	m_pAbortByUser = abortFlag;
}

#if ENABLE_MVC_ENCODING
void CEncodingPipeline::SetMultiView()
{
	m_pFileReader->SetMultiView();
	m_bIsMVC = true;
}
#endif
mfxStatus CEncodingPipeline::InitInOut(sInputParams *pParams)
{
	mfxStatus sts = MFX_ERR_NONE;

	//prepare for LogFile
	if (pParams->pStrLogFile) {
		int logFilenameLen = (int)_tcslen(pParams->pStrLogFile);
		if (NULL == (m_pStrLog = (TCHAR *)calloc(logFilenameLen + 1, sizeof(m_pStrLog[0])))) {
			PrintMes(_T("Failed to set log file.\n"));
		} else {
			_tcscpy_s(m_pStrLog, logFilenameLen + 1, pParams->pStrLogFile);

			FILE *fp_log = NULL;
			if (_tfopen_s(&fp_log, m_pStrLog, _T("a")) || fp_log == NULL) {
				m_pStrLog = NULL; //disable log file output
				PrintMes(_T("Failed to open log file.\n"));
			} else {
				int dstFilenameLen = (int)_tcslen(pParams->strDstFile);
				static const char *const SEP5 = "-----";
				int sep_count = max(16, dstFilenameLen / 5 + 1);
				for (int i = 0; i < sep_count; i++)
					fprintf(fp_log, "%s", SEP5);
				fprintf(fp_log, "\n");
#ifdef UNICODE
				int buffer_size = (dstFilenameLen + 1) * 2;
				char *buffer_char = (char *)calloc(buffer_size, sizeof(buffer_char[0]));
				if (buffer_char) {
					WideCharToMultiByte(CP_THREAD_ACP, WC_NO_BEST_FIT_CHARS, pParams->strDstFile, -1, buffer_char, buffer_size, NULL, NULL);
					fprintf(fp_log, " %s\n", buffer_char);
					free(buffer_char);
				}
#else
				fprintf(fp_log, " %s\n", pParams->strSrcFile);
#endif
				for (int i = 0; i < sep_count; i++)
					fprintf(fp_log, "%s", SEP5);
				fprintf(fp_log, "\n");
				fclose(fp_log);
			}
		}
	}

	m_pEncSatusInfo = new CEncodeStatusInfo();

	//Auto detection by input file extension
	if (pParams->nInputFmt == INPUT_FMT_AUTO) {
#if ENABLE_AVISYNTH_READER
		if (   0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".avs")))
			pParams->nInputFmt = INPUT_FMT_AVS;
		else
#endif //ENABLE_AVISYNTH_READER
#if ENABLE_VAPOURSYNTH_READER
		if (   0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".vpy")))
			pParams->nInputFmt = INPUT_FMT_VPY;
		else
#endif //ENABLE_VAPOURSYNTH_READER
#if ENABLE_AVI_READER
		if (   0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".avi"))
			|| 0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".avs"))
			|| 0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".vpy")))
			pParams->nInputFmt = INPUT_FMT_AVI;
		else
#endif //ENABLE_AVI_READER
		if (   0 == _tcsicmp(PathFindExtension(pParams->strSrcFile), _T(".y4m")))
			pParams->nInputFmt = INPUT_FMT_Y4M;
	}

	//Check if selected format is enabled
	if (pParams->nInputFmt == INPUT_FMT_AVS && !ENABLE_AVISYNTH_READER) {
		pParams->nInputFmt = INPUT_FMT_AVI;
		PrintMes(_T("avs reader not compiled in this binary.\n"));
		PrintMes(_T("switching to avi reader.\n"));
	}
	if (pParams->nInputFmt == INPUT_FMT_VPY && !ENABLE_VAPOURSYNTH_READER) {
		pParams->nInputFmt = INPUT_FMT_AVI;
		PrintMes(_T("vpy reader not compiled in this binary.\n"));
		PrintMes(_T("switching to avi reader.\n"));
	}
	if (pParams->nInputFmt == INPUT_FMT_VPY_MT && !ENABLE_VAPOURSYNTH_READER) {
		pParams->nInputFmt = INPUT_FMT_AVI;
		PrintMes(_T("vpy reader not compiled in this binary.\n"));
		PrintMes(_T("switching to avi reader.\n"));
	}
	if (pParams->nInputFmt == INPUT_FMT_AVI && !ENABLE_AVI_READER) {
		PrintMes(_T("avi reader not compiled in this binary.\n"));
		return MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;
	}

	//try to setup avs or vpy reader
	m_pFileReader = NULL;
	if (   pParams->nInputFmt == INPUT_FMT_VPY
		|| pParams->nInputFmt == INPUT_FMT_VPY_MT
		|| pParams->nInputFmt == INPUT_FMT_AVS) {

		if (pParams->nInputFmt == INPUT_FMT_VPY || pParams->nInputFmt == INPUT_FMT_VPY_MT) {
#if ENABLE_VAPOURSYNTH_READER
			m_pFileReader = new CVSReader();
#endif
		} else {
#if ENABLE_AVISYNTH_READER
			m_pFileReader = new CAVSReader();
#endif
		}
		if (NULL == m_pFileReader) {
			//switch to avi reader and retry
			pParams->nInputFmt = INPUT_FMT_AVI;
		} else {
			sts = m_pFileReader->Init(pParams->strSrcFile, pParams->ColorFormat, pParams->nInputFmt == INPUT_FMT_VPY_MT,
				&m_EncThread, m_pEncSatusInfo, &pParams->sInCrop);
			if (sts == MFX_ERR_INVALID_COLOR_FORMAT) {
				//if failed because of colorformat, switch to avi reader and retry.
				PrintMes(m_pFileReader->GetInputMessage());
				delete m_pFileReader;
				m_pFileReader = NULL;
				sts = MFX_ERR_NONE;
				PrintMes(_T("switching to avi reader.\n"));
				pParams->nInputFmt = INPUT_FMT_AVI;
			}
			if (sts < MFX_ERR_NONE)
				PrintMes(m_pFileReader->GetInputMessage());
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
		}
	}

	if (NULL == m_pFileReader) {
		switch (pParams->nInputFmt) {
#if ENABLE_AVI_READER
			case INPUT_FMT_AVI:  m_pFileReader = new CAVIReader(); break;
#endif
			case INPUT_FMT_Y4M:
			case INPUT_FMT_RAW:
			default: m_pFileReader = new CSmplYUVReader(); break;
		}
		sts = m_pFileReader->Init(pParams->strSrcFile, pParams->ColorFormat, pParams->nInputFmt == INPUT_FMT_Y4M,
			&m_EncThread, m_pEncSatusInfo, &pParams->sInCrop);
	}
	if (sts < MFX_ERR_NONE)
		PrintMes(m_pFileReader->GetInputMessage());
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// prepare output file writer
	m_pFileWriter = new CSmplBitstreamWriter();
	sts = m_pFileWriter->Init(pParams->strDstFile, pParams, m_pEncSatusInfo);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	return sts;
}

mfxStatus CEncodingPipeline::DetermineMinimumRequiredVersion(const sInputParams &pParams, mfxVersion &version)
{
	version.Major = 1;
	version.Minor = 0;

	if (MVC_DISABLED != pParams.MVC_flags)
		version.Minor = 3;
	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::CheckParam(sInputParams *pParams) {
	mfxFrameInfo inputFrameInfo = { 0 };
	m_pFileReader->GetInputFrameInfo(&inputFrameInfo);

	sInputCrop cropInfo = { 0 };
	m_pFileReader->GetInputCropInfo(&cropInfo);

	//Get Info From Input
	if (inputFrameInfo.Width)
		pParams->nWidth = inputFrameInfo.Width;

	if (inputFrameInfo.Height)
		pParams->nHeight = inputFrameInfo.Height;

	if (inputFrameInfo.PicStruct)
		pParams->nPicStruct = inputFrameInfo.PicStruct;

	if ((!pParams->nPAR[0] || !pParams->nPAR[1]) && inputFrameInfo.AspectRatioW && inputFrameInfo.AspectRatioH) {
		pParams->nPAR[0] = inputFrameInfo.AspectRatioW;
		pParams->nPAR[1] = inputFrameInfo.AspectRatioH;
	}
	if ((!pParams->nFPSRate || !pParams->nFPSScale) && inputFrameInfo.FrameRateExtN && inputFrameInfo.FrameRateExtD) {
		pParams->nFPSRate = inputFrameInfo.FrameRateExtN;
		pParams->nFPSScale = inputFrameInfo.FrameRateExtD;
	}


	//Checking Start...
	//if picstruct not set, progressive frame is expected
	if (!pParams->nPicStruct) {
		pParams->nPicStruct = MFX_PICSTRUCT_PROGRESSIVE;
	}

	//don't use d3d memory with software encoding
	if (!pParams->bUseHWLib) {
		pParams->memType = SYSTEM_MEMORY;
	}

	int h_mul = 2;
	bool output_interlaced = ((pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)) != 0 && !pParams->vpp.nDeinterlace);
	if (output_interlaced)
		h_mul *= 2;
	//check for crop settings
	if (pParams->sInCrop.left % 2 != 0 || pParams->sInCrop.right % 2 != 0)
	{
		PrintMes(_T("crop width should be a multiple of 2.\n"));
		return MFX_PRINT_OPTION_ERR;
	}
	if (pParams->sInCrop.bottom % h_mul != 0 || pParams->sInCrop.up % h_mul != 0)
	{
		PrintMes(_T("crop height should be a multiple of %d.\n"));
		return MFX_PRINT_OPTION_ERR;
	}
	if (0 == pParams->nWidth || 0 == pParams->nHeight) {
		PrintMes(_T("--input-res must be specified with raw input.\n"));
		return MFX_PRINT_OPTION_ERR;
	}
	if (pParams->nFPSRate == 0 || pParams->nFPSScale == 0) {
		PrintMes(_T("--fps must be specified with raw input.\n"));
		return MFX_PRINT_OPTION_ERR;
	}
	if (   pParams->nWidth < (pParams->sInCrop.left + pParams->sInCrop.right)
		|| pParams->nHeight < (pParams->sInCrop.bottom + pParams->sInCrop.up)) {
			PrintMes(_T("crop size is too big.\n"));
			return MFX_PRINT_OPTION_ERR;
	}
	pParams->nWidth -= (pParams->sInCrop.left + pParams->sInCrop.right);
	pParams->nHeight -= (pParams->sInCrop.bottom + pParams->sInCrop.up);

	// if no destination picture width or height wasn't specified set it to the source picture size
	if (pParams->nDstWidth == 0)
		pParams->nDstWidth = pParams->nWidth;

	if (pParams->nDstHeight == 0)
		pParams->nDstHeight = pParams->nHeight;

	if (pParams->nDstHeight != pParams->nHeight || pParams->nDstWidth != pParams->nWidth) {
		pParams->vpp.bEnable = true;
		pParams->vpp.bUseResize = true;
	}
	if (pParams->nDstWidth % 2 != 0)
	{
		PrintMes(_T("output width should be a multiple of 2."));
		return MFX_PRINT_OPTION_ERR;
	}

	if (pParams->nDstHeight % h_mul != 0)
	{
		PrintMes(_T("output height should be a multiple of %d."), h_mul);
		return MFX_PRINT_OPTION_ERR;
	}

	//Cehck For Framerate
	if (pParams->nFPSRate == 0 || pParams->nFPSScale == 0) {
		PrintMes(_T("unable to parse fps data.\n"));
		return MFX_PRINT_OPTION_ERR;
	}
	mfxU32 OutputFPSRate = pParams->nFPSRate;
	mfxU32 OutputFPSScale = pParams->nFPSScale;
	mfxU32 outputFrames = *(mfxU32 *)&inputFrameInfo.FrameId;
	if ((pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
		switch (pParams->vpp.nDeinterlace) {
		case MFX_DEINTERLACE_IT:
			OutputFPSRate = OutputFPSRate * 4;
			OutputFPSScale = OutputFPSScale * 5;
			outputFrames *= 4 / 5;
			break;
		case MFX_DEINTERLACE_BOB:
			OutputFPSRate = OutputFPSRate * 2;
			outputFrames *= 2;
			break;
		default:
			break;
		}
	}
	mfxU32 gcd = GCD(OutputFPSRate, OutputFPSScale);
	OutputFPSRate /= gcd;
	OutputFPSScale /= gcd;
	m_pEncSatusInfo->Init(OutputFPSRate, OutputFPSScale, outputFrames, m_pStrLog);

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::InitSession(bool useHWLib, mfxU16 memType) {
	mfxStatus sts = MFX_ERR_NONE;
	// init session, and set memory type
    mfxIMPL impl = 0;
	mfxVersion verRequired = MFX_LIB_VERSION_1_1;
	m_mfxSession.Close();
    if (useHWLib)
    {
        // try searching on all display adapters
        impl = MFX_IMPL_HARDWARE_ANY;
		m_memType = D3D9_MEMORY;

		//Win7でD3D11のチェックをやると、
		//デスクトップコンポジションが切られてしまう問題が発生すると報告を頂いたので、
		//D3D11をWin8以降に限定
		if (!check_OS_Win8orLater())
			memType &= (~D3D11_MEMORY);

		//D3D11モードは基本的には遅い模様なので、自動モードなら切る
		if (HW_MEMORY == (memType & HW_MEMORY) && false == check_if_d3d11_necessary())
			memType &= (~D3D11_MEMORY);

		for (int i_try_d3d11 = 0; i_try_d3d11 < 1 + (HW_MEMORY == (memType & HW_MEMORY)); i_try_d3d11++) {
			// if d3d11 surfaces are used ask the library to run acceleration through D3D11
			// feature may be unsupported due to OS or MSDK API version
#if MFX_D3D11_SUPPORT
			if (D3D11_MEMORY & memType) {
				if (0 == i_try_d3d11) {
					impl |= MFX_IMPL_VIA_D3D11; //first try with d3d11 memory
					m_memType = D3D11_MEMORY;
				} else {
					impl &= ~MFX_IMPL_VIA_D3D11; //turn of d3d11 flag and retry
				 	m_memType = D3D9_MEMORY;
				}
			}
#endif
			sts = m_mfxSession.Init(impl, &verRequired);

			// MSDK API version may not support multiple adapters - then try initialize on the default
			if (MFX_ERR_NONE != sts)
				sts = m_mfxSession.Init(impl & !MFX_IMPL_HARDWARE_ANY | MFX_IMPL_HARDWARE, &verRequired);

			if (MFX_ERR_NONE == sts)
				break;
		}
    } else {
        impl = MFX_IMPL_SOFTWARE;
        sts = m_mfxSession.Init(impl, &verRequired);
		m_memType = SYSTEM_MEMORY;
	}
	//使用できる最大のversionをチェック
	m_mfxVer = get_mfx_lib_version(impl);
	return sts;
}

mfxStatus CEncodingPipeline::Init(sInputParams *pParams)
{
	MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);

	mfxStatus sts = MFX_ERR_NONE;

	//Haswell以降かをチェック
	m_bHaswellOrLater = isHaswellOrLater();

	sts = m_EncThread.Init(pParams->nInputBufSize);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitInOut(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = CheckParam(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitSession(pParams->bUseHWLib, pParams->memType);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //Load library plug-in
    //if (pParams->IsUseHEVCEncoderPlugin)
    //{
    //    MSDK_MEMCPY(m_UID_HEVC.Data, HEVC_ENCODER_UID, 16);
    //    sts = MFXVideoUSER_Load(m_mfxSession, &m_UID_HEVC, pParams->HEVCPluginVersion);
    //}
   //MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// create and init frame allocator
	sts = CreateAllocator();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitMfxEncParams(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = InitMfxVppParams(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = CreateVppExtBuffers(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

#if ENABLE_MVC_ENCODING
	sts = AllocAndInitMVCSeqDesc();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// MVC specific options
	if (MVC_ENABLED & m_MVCflags)
	{
		sts = AllocAndInitMVCSeqDesc();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}
#endif

	// シーンチェンジ検出
	bool input_interlaced = 0 != (pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF));
	bool deinterlace_enabled = input_interlaced && (pParams->vpp.nDeinterlace != MFX_DEINTERLACE_NONE);
	bool deinterlace_normal = input_interlaced && (pParams->vpp.nDeinterlace == MFX_DEINTERLACE_NORMAL);
	if (m_nExPrm & (MFX_PRM_EX_VQP | MFX_PRM_EX_SCENE_CHANGE))
		if (m_SceneChange.Init(80, (deinterlace_enabled) ? m_mfxVppParams.mfx.FrameInfo.PicStruct : m_mfxEncParams.mfx.FrameInfo.PicStruct, pParams->nVQPStrength, pParams->nVQPSensitivity, 3, pParams->nGOPLength, deinterlace_normal))
			MSDK_CHECK_RESULT(MFX_ERR_UNDEFINED_BEHAVIOR, MFX_ERR_NONE, MFX_ERR_UNDEFINED_BEHAVIOR);

	// create encoder
	m_pmfxENC = new MFXVideoENCODE(m_mfxSession);
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_MEMORY_ALLOC);

	// create preprocessor if resizing was requested from command line
	// or if different FourCC is set in InitMfxVppParams
	if (pParams->nWidth  != pParams->nDstWidth ||
		pParams->nHeight != pParams->nDstHeight ||
		m_mfxVppParams.vpp.In.FourCC != m_mfxVppParams.vpp.Out.FourCC ||
		m_mfxVppParams.NumExtParam > 1 ||
		pParams->vpp.nDeinterlace
		)
	{
		m_pmfxVPP = new MFXVideoVPP(m_mfxSession);
		MSDK_CHECK_POINTER(m_pmfxVPP, MFX_ERR_MEMORY_ALLOC);
	}
	if (m_mfxVppParams.vpp.In.FourCC != m_mfxVppParams.vpp.Out.FourCC) {
		VppExtMes += _T("ColorFmtConvertion: ");
		VppExtMes += ColorFormatToStr(m_mfxVppParams.vpp.In.FourCC);
		VppExtMes += _T(" -> ");
		VppExtMes += ColorFormatToStr(m_mfxVppParams.vpp.Out.FourCC);
		VppExtMes += _T("\n");
	}
	if (pParams->nWidth  != pParams->nDstWidth ||
		pParams->nHeight != pParams->nDstHeight) {
		TCHAR mes[256];
		_stprintf_s(mes, _countof(mes), _T("Resizer, %dx%d -> %dx%d\n"), pParams->nWidth, pParams->nHeight, pParams->nDstWidth, pParams->nDstHeight);
		VppExtMes += mes;
	}

	m_nAsyncDepth = 3; // this number can be tuned for better performance

	sts = ResetMFXComponents(pParams);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	return MFX_ERR_NONE;
}

void CEncodingPipeline::Close()
{
	//_ftprintf(stderr, _T("Frame number: %hd\r"), m_pFileWriter.m_nProcessedFramesNum);

	MSDK_SAFE_DELETE(m_pEncSatusInfo);
	m_EncThread.Close();

	MSDK_SAFE_DELETE(m_pmfxENC);
	MSDK_SAFE_DELETE(m_pmfxVPP);

    MFXVideoUSER_UnLoad(m_mfxSession, &m_UID_HEVC);
    m_pHEVC_plugin.reset();

#if ENABLE_MVC_ENCODING
	FreeMVCSeqDesc();
#endif
	//FreeVppDoNotUse();

	m_EncExtParams.clear();
	m_VppDoNotUseList.clear();
	m_VppDoUseList.clear();
	m_VppExtParams.clear();
	VppExtMes.clear();
	DeleteFrames();
	// allocator if used as external for MediaSDK must be deleted after SDK components
	DeleteAllocator();

	m_TaskPool.Close();
	m_mfxSession.Close();

	m_SceneChange.Close();

	if (m_pStrLog) {
		FILE *fp_log = NULL;
		if (0 == _tfopen_s(&fp_log, m_pStrLog, _T("a")) && fp_log) {
			fprintf(fp_log, "\n\n");
			fclose(fp_log);
		}
		free(m_pStrLog);
		m_pStrLog = NULL;
	}

	if (m_pFileWriter) {
		m_pFileWriter->Close();
		delete m_pFileWriter;
		m_pFileWriter = NULL;
	}

	if (m_pFileReader) {
		m_pFileReader->Close();
		delete m_pFileReader;
		m_pFileReader = NULL;
	}
	
	m_pAbortByUser = NULL;
	m_bHaswellOrLater = false;
	m_nExPrm = 0x00;
}

mfxStatus CEncodingPipeline::ResetMFXComponents(sInputParams* pParams)
{
	MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;

	sts = m_pmfxENC->Close();
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_NOT_INITIALIZED);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (m_pmfxVPP)
	{
		sts = m_pmfxVPP->Close();
		MSDK_IGNORE_MFX_STS(sts, MFX_ERR_NOT_INITIALIZED);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	// free allocated frames
	DeleteFrames();

	m_TaskPool.Close();

	sts = AllocFrames();
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = m_pmfxENC->Init(&m_mfxEncParams);
	if (MFX_WRN_PARTIAL_ACCELERATION == sts)
	{
		msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
		MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
	}
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (m_pmfxVPP)
	{
		sts = m_pmfxVPP->Init(&m_mfxVppParams);
		if (MFX_WRN_PARTIAL_ACCELERATION == sts)
		{
			msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
			MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
		}
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	mfxU32 nEncodedDataBufferSize = m_mfxEncParams.mfx.FrameInfo.Width * m_mfxEncParams.mfx.FrameInfo.Height * 4;
	sts = m_TaskPool.Init(&m_mfxSession, m_pFileWriter, m_nAsyncDepth * 2, nEncodedDataBufferSize, NULL);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::AllocateSufficientBuffer(mfxBitstream* pBS)
{
	MSDK_CHECK_POINTER(pBS, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxVideoParam par;
	MSDK_ZERO_MEMORY(par);

	// find out the required buffer size
	mfxStatus sts = m_pmfxENC->GetVideoParam(&par);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// reallocate bigger buffer for output
	sts = ExtendMfxBitstream(pBS, par.mfx.BufferSizeInKB * 1000);
	MSDK_CHECK_RESULT_SAFE(sts, MFX_ERR_NONE, sts, WipeMfxBitstream(pBS));

	return MFX_ERR_NONE;
}

mfxStatus CEncodingPipeline::GetFreeTask(sTask **ppTask)
{
	mfxStatus sts = MFX_ERR_NONE;

	sts = m_TaskPool.GetFreeTask(ppTask);
	if (MFX_ERR_NOT_FOUND == sts)
	{
		sts = SynchronizeFirstTask();
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

		// try again
		sts = m_TaskPool.GetFreeTask(ppTask);
	}

	return sts;
}

mfxStatus CEncodingPipeline::SynchronizeFirstTask()
{
	mfxStatus sts = m_TaskPool.SynchronizeFirstTask();

	return sts;
}

mfxStatus CEncodingPipeline::CheckSceneChange()
{
	mfxStatus sts = MFX_ERR_NONE;
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	const int bufferSize = m_EncThread.m_nFrameBuffer;
	sInputBufSys *pArrayInputBuf = m_EncThread.m_InputBuf;
	sInputBufSys *pInputBuf;

	mfxVideoParam videoPrm;
	MSDK_ZERO_MEMORY(videoPrm);
	m_pmfxENC->GetVideoParam(&videoPrm);

	m_frameTypeSim.Init(videoPrm.mfx.GopPicSize, videoPrm.mfx.GopRefDist-1, videoPrm.mfx.QPI, videoPrm.mfx.QPP, videoPrm.mfx.QPB);
	//bool bInterlaced = (0 != (videoPrm.mfx.FrameInfo.PicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)));
	mfxU32 lastFrameFlag = 0;

	//入力ループ
	for (mfxU32 i_frames = 0; !m_EncThread.m_bthSubAbort; i_frames++) {
		pInputBuf = &pArrayInputBuf[i_frames % bufferSize];
		WaitForSingleObject(pInputBuf->heSubStart, INFINITE);

		m_EncThread.m_bthSubAbort |= ((m_EncThread.m_stsThread == MFX_ERR_MORE_DATA && i_frames == m_pEncSatusInfo->m_nInputFrames));

		if (!m_EncThread.m_bthSubAbort) {
			//フレームタイプとQP値の決定
			int qp_offset[2] = { 0, 0 };
			mfxU32 frameFlag = m_SceneChange.Check(pInputBuf->pFrameSurface, qp_offset);
			_InterlockedExchange((long *)&pInputBuf->frameFlag, frameFlag);
			if (m_nExPrm & MFX_PRM_EX_VQP)
				_InterlockedExchange((long *)&pInputBuf->AQP[0], m_frameTypeSim.CurrentQP(!!((frameFlag | (lastFrameFlag>>8)) & MFX_FRAMETYPE_IDR), qp_offset[0]));
			m_frameTypeSim.ToNextFrame();
			if (m_nExPrm & MFX_PRM_EX_DEINT_BOB) {
				if (m_nExPrm & MFX_PRM_EX_VQP)
					_InterlockedExchange((long *)&pInputBuf->AQP[1], m_frameTypeSim.CurrentQP(!!(frameFlag & MFX_FRAMETYPE_xIDR), qp_offset[1]));
				m_frameTypeSim.ToNextFrame();
			}
			if (m_nExPrm & MFX_PRM_EX_DEINT_NORMAL) {
				lastFrameFlag = frameFlag;
			}
		}

		SetEvent(pInputBuf->heInputDone);
	}

	return sts;
}

unsigned int __stdcall CEncodingPipeline::RunEncThreadLauncher(void *pParam) {
	reinterpret_cast<CEncodingPipeline*>(pParam)->RunEncode();
	_endthreadex(0);
	return 0;
}

unsigned int __stdcall CEncodingPipeline::RunSubThreadLauncher(void *pParam) {
	reinterpret_cast<CEncodingPipeline*>(pParam)->CheckSceneChange();
	_endthreadex(0);
	return 0;
}

mfxStatus CEncodingPipeline::Run()
{
	return Run(NULL);
}

mfxStatus CEncodingPipeline::Run(DWORD_PTR SubThreadAffinityMask)
{
	mfxStatus sts = MFX_ERR_NONE;
	sts = m_EncThread.RunEncFuncbyThread(RunEncThreadLauncher, this, SubThreadAffinityMask);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	if (m_SceneChange.isInitialized()) {
		sts = m_EncThread.RunSubFuncbyThread(RunSubThreadLauncher, this, SubThreadAffinityMask);
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	const int bufferSize = m_EncThread.m_nFrameBuffer;
	sInputBufSys *pArrayInputBuf = m_EncThread.m_InputBuf;
	sInputBufSys *pInputBuf;
	//入力ループ
	for (int i = 0; sts == MFX_ERR_NONE; i++) {
		pInputBuf = &pArrayInputBuf[i % bufferSize];
		//_ftprintf(stderr, "run loop: wait for %d\n", i);
		//_ftprintf(stderr, "wait for heInputStart %d\n", i);
		while (WAIT_TIMEOUT == WaitForSingleObject(pInputBuf->heInputStart, 10000)) {
			DWORD exit_code = 0;
			if (0 == GetExitCodeThread(m_EncThread.GetHandleEncThread(), &exit_code) || exit_code != STILL_ACTIVE) {
				PrintMes(_T("error at encode thread.\n"));
				sts = MFX_ERR_INVALID_HANDLE;
				break;
			}
			if (m_SceneChange.isInitialized()
				&& (0 == GetExitCodeThread(m_EncThread.GetHandleSubThread(), &exit_code) || exit_code != STILL_ACTIVE)) {
					PrintMes(_T("error at sub thread.\n"));
					sts = MFX_ERR_INVALID_HANDLE;
					break;
			}
		}
		//_ftprintf(stderr, "load next frame %d to %d\n", i, pInputBuf->pFrameSurface);
		if (!sts)
			sts = m_pFileReader->LoadNextFrame(pInputBuf->pFrameSurface);
		if (NULL != m_pAbortByUser && *m_pAbortByUser) {
			PrintMes(_T("                                                                         \r"));
			sts = MFX_ERR_ABORTED;
		}
		//_ftprintf(stderr, "set for heInputDone %d\n", i);
		SetEvent((m_SceneChange.isInitialized()) ? pInputBuf->heSubStart : pInputBuf->heInputDone);
	}
	m_EncThread.WaitToFinish(sts);

	sFrameTypeInfo info = { 0 };
	if (m_nExPrm & MFX_PRM_EX_VQP)
		m_frameTypeSim.getFrameInfo(&info);
	m_pEncSatusInfo->WriteResults((m_nExPrm & MFX_PRM_EX_VQP) ? &info : NULL);

	sts = min(sts, m_EncThread.m_stsThread);
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);

	if (sts != MFX_ERR_NONE)
		PrintMes(_T("%s\n"), get_err_mes(sts));

	m_EncThread.Close();

	return sts;
}

mfxStatus CEncodingPipeline::RunEncode()
{
	MSDK_CHECK_POINTER(m_pmfxENC, MFX_ERR_NOT_INITIALIZED);

	mfxStatus sts = MFX_ERR_NONE;

	mfxFrameSurface1* pSurfInputBuf = NULL;
	mfxFrameSurface1* pSurfEncIn = NULL;
	mfxFrameSurface1 *pSurfVppIn = NULL;
	mfxFrameSurface1 **ppNextFrame;

	sTask *pCurrentTask = NULL; // a pointer to the current task
	mfxU16 nEncSurfIdx = 0; // index of free surface for encoder input (vpp output)
	mfxU16 nVppSurfIdx = 0; // index of free surface for vpp input

	mfxSyncPoint VppSyncPoint = NULL; // a sync point associated with an asynchronous vpp call
	bool bVppMultipleOutput = false;  // this flag is true if VPP produces more frames at output
									  // than consumes at input. E.g. framerate conversion 30 fps -> 60 fps

	mfxU16 nLastFrameFlag = 0;
	int nLastAQ = 0;
	bool bVppDeintBobFirstFeild = true;

	m_pEncSatusInfo->SetStart();

#if ENABLE_MVC_ENCODING
	// Since in sample we support just 2 views
	// we will change this value between 0 and 1 in case of MVC
	mfxU16 currViewNum = 0;
#endif

	sts = MFX_ERR_NONE;

	//先読みバッファ用フレーム
	if (m_pmfxVPP)
	{
		for (int i = 0; i < m_EncThread.m_nFrameBuffer; i++)
		{
			nVppSurfIdx = GetFreeSurface(m_pVppSurfaces, m_VppResponse.NumFrameActual);
			MSDK_CHECK_ERROR(nVppSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);
			if (m_bExternalAlloc)
			{
				sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_pVppSurfaces[nVppSurfIdx].Data.MemId, &(m_pVppSurfaces[nVppSurfIdx].Data));
				MSDK_BREAK_ON_ERROR(sts);
			}
			m_pFileReader->SetNextSurface(&m_pVppSurfaces[nVppSurfIdx]);
#if ENABLE_MVC_ENCODING
			m_pEncSurfaces[nEncSurfIdx].Info.FrameId.ViewId = currViewNum;
			if (m_bIsMVC) currViewNum ^= 1; // Flip between 0 and 1 for ViewId
#endif
		}
	}
	else
	{
		for (int i = 0; i < m_EncThread.m_nFrameBuffer; i++)
		{
			nEncSurfIdx = GetFreeSurface(m_pEncSurfaces, m_EncResponse.NumFrameActual);
			MSDK_CHECK_ERROR(nEncSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);
			if (m_bExternalAlloc)
			{
				sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_pEncSurfaces[nEncSurfIdx].Data.MemId, &(m_pEncSurfaces[nEncSurfIdx].Data));
				MSDK_BREAK_ON_ERROR(sts);
			}
			m_pFileReader->SetNextSurface(&m_pEncSurfaces[nEncSurfIdx]);
#if ENABLE_MVC_ENCODING
			m_pEncSurfaces[nEncSurfIdx].Info.FrameId.ViewId = currViewNum;
			if (m_bIsMVC) currViewNum ^= 1; // Flip between 0 and 1 for ViewId
#endif
		}
	}

	// main loop, preprocessing and encoding
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)
	{
		// get a pointer to a free task (bit stream and sync point for encoder)
		sts = GetFreeTask(&pCurrentTask);
		MSDK_BREAK_ON_ERROR(sts);

		// find free surface for encoder input
		nEncSurfIdx = GetFreeSurface(m_pEncSurfaces, m_EncResponse.NumFrameActual);
		MSDK_CHECK_ERROR(nEncSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);

		// point pSurf to encoder surface
		pSurfEncIn = &m_pEncSurfaces[nEncSurfIdx];

		if (!bVppMultipleOutput)
		{
			// if vpp is enabled find free surface for vpp input and point pSurf to vpp surface
			if (m_pmfxVPP)
			{
				nVppSurfIdx = GetFreeSurface(m_pVppSurfaces, m_VppResponse.NumFrameActual);
				MSDK_CHECK_ERROR(nVppSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);

				pSurfInputBuf = &m_pVppSurfaces[nVppSurfIdx];
				ppNextFrame = &pSurfVppIn;
			}
			else
			{
				pSurfInputBuf = pSurfEncIn;
				ppNextFrame = &pSurfEncIn;
			}
			sts = m_pFileReader->GetNextFrame(ppNextFrame);
			MSDK_BREAK_ON_ERROR(sts);

			if (m_bExternalAlloc)
			{
				sts = m_pMFXAllocator->Unlock(m_pMFXAllocator->pthis, (*ppNextFrame)->Data.MemId, &((*ppNextFrame)->Data));
				MSDK_BREAK_ON_ERROR(sts);

				sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, pSurfInputBuf->Data.MemId, &(pSurfInputBuf->Data));
				MSDK_BREAK_ON_ERROR(sts);
			}

			m_pFileReader->SetNextSurface(pSurfInputBuf);
#if ENABLE_MVC_ENCODING
			pSurfInputBuf->Info.FrameId.ViewId = currViewNum;
			if (m_bIsMVC) currViewNum ^= 1; // Flip between 0 and 1 for ViewId
#endif
		}

		// perform preprocessing if required
		if (m_pmfxVPP)
		{
			bVppMultipleOutput = false; // reset the flag before a call to VPP
			for (;;)
			{
				sts = m_pmfxVPP->RunFrameVPPAsync(pSurfVppIn, pSurfEncIn, NULL, &VppSyncPoint);

				if (MFX_ERR_NONE < sts && !VppSyncPoint) // repeat the call if warning and no output
				{
					if (MFX_WRN_DEVICE_BUSY == sts)
						Sleep(1); // wait if device is busy
				}
				else if (MFX_ERR_NONE < sts && VppSyncPoint)
				{
					sts = MFX_ERR_NONE; // ignore warnings if output is available
					break;
				}
				else
					break; // not a warning
			}

			// process errors
			if (MFX_ERR_MORE_DATA == sts)
			{
				continue;
			}
			else if (MFX_ERR_MORE_SURFACE == sts)
			{
				bVppMultipleOutput = true;
			}
			else
			{
				MSDK_BREAK_ON_ERROR(sts);
			}
		}

		// save the id of preceding vpp task which will produce input data for the encode task
		if (VppSyncPoint)
		{
			pCurrentTask->DependentVppTasks.push_back(VppSyncPoint);
			VppSyncPoint = NULL;
		}
		
		bool bDeviceBusy = false;
		mfxEncodeCtrl *ptrCtrl = NULL;
		mfxEncodeCtrl encCtrl = { 0 };
		for (;;)
		{
			// at this point surface for encoder contains either a frame from file or a frame processed by vpp
			if (!bDeviceBusy && m_nExPrm & (MFX_PRM_EX_SCENE_CHANGE | MFX_PRM_EX_VQP)) {
				if (m_nExPrm & MFX_PRM_EX_DEINT_NORMAL) {
					mfxU32 currentFrameFlag = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
					if (nLastFrameFlag >> 8) {
						encCtrl.FrameType = nLastFrameFlag >> 8;
						encCtrl.QP = (mfxU16)nLastAQ;
					} else {
						encCtrl.FrameType = currentFrameFlag & 0xff;
						encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
					}
					nLastFrameFlag = (mfxU16)currentFrameFlag;
					nLastAQ = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[1];
					pSurfEncIn->Data.TimeStamp = 0;
				} else if (m_nExPrm & MFX_PRM_EX_DEINT_BOB) {
					if (bVppDeintBobFirstFeild) {
						nLastFrameFlag = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
						nLastAQ = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[1];
						encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
						encCtrl.FrameType = nLastFrameFlag & 0xff;
						pSurfEncIn->Data.TimeStamp = 0;
					} else {
						encCtrl.FrameType = nLastFrameFlag >> 8;
						encCtrl.QP = (mfxU16)nLastAQ;
					}
					bVppDeintBobFirstFeild ^= true;
				} else {
					encCtrl.FrameType = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
					encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
					pSurfEncIn->Data.TimeStamp = 0;
				}
				ptrCtrl = &encCtrl;
			}
			sts = m_pmfxENC->EncodeFrameAsync(ptrCtrl, pSurfEncIn, &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);
			bDeviceBusy = false;

			if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
			{
				bDeviceBusy = true;
				if (MFX_WRN_DEVICE_BUSY == sts)
					Sleep(1); // wait if device is busy
			}
			else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
			{
				sts = MFX_ERR_NONE; // ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
			{
				sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
				MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
			}
			else
			{
				// get next surface and new task for 2nd bitstream in ViewOutput mode
				MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
				break;
			}
		}
	}

	// means that the input file has ended, need to go to buffering loops
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	// exit in case of other errors
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	if (m_pmfxVPP)
	{
		// loop to get buffered frames from vpp
		while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts)
			// MFX_ERR_MORE_SURFACE can be returned only by RunFrameVPPAsync
				// MFX_ERR_MORE_DATA is accepted only from EncodeFrameAsync
		{
			// find free surface for encoder input (vpp output)
			nEncSurfIdx = GetFreeSurface(m_pEncSurfaces, m_EncResponse.NumFrameActual);
			MSDK_CHECK_ERROR(nEncSurfIdx, MSDK_INVALID_SURF_IDX, MFX_ERR_MEMORY_ALLOC);

			for (;;)
			{
				sts = m_pmfxVPP->RunFrameVPPAsync(NULL, &m_pEncSurfaces[nEncSurfIdx], NULL, &VppSyncPoint);

				if (MFX_ERR_NONE < sts && !VppSyncPoint) // repeat the call if warning and no output
				{
					if (MFX_WRN_DEVICE_BUSY == sts)
						Sleep(1); // wait if device is busy
				}
				else if (MFX_ERR_NONE < sts && VppSyncPoint)
				{
					sts = MFX_ERR_NONE; // ignore warnings if output is available
					break;
				}
				else
					break; // not a warning
			}

			if (MFX_ERR_MORE_SURFACE == sts)
			{
				continue;
			}
			else
			{
				MSDK_BREAK_ON_ERROR(sts);
			}

			// get a free task (bit stream and sync point for encoder)
			sts = GetFreeTask(&pCurrentTask);
			MSDK_BREAK_ON_ERROR(sts);

			// save the id of preceding vpp task which will produce input data for the encode task
			if (VppSyncPoint)
			{
				pCurrentTask->DependentVppTasks.push_back(VppSyncPoint);
				VppSyncPoint = NULL;
			}
			
			bool bDeviceBusy = false;
			mfxEncodeCtrl *ptrCtrl = NULL;
			mfxEncodeCtrl encCtrl = { 0 };
			for (;;)
			{
				// at this point surface for encoder contains either a frame from file or a frame processed by vpp
				if (!bDeviceBusy && m_nExPrm & (MFX_PRM_EX_SCENE_CHANGE | MFX_PRM_EX_VQP)) {
					if (m_nExPrm & MFX_PRM_EX_DEINT_NORMAL) {
						mfxU32 currentFrameFlag = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
						if (nLastFrameFlag >> 8) {
							encCtrl.FrameType = nLastFrameFlag >> 8;
							encCtrl.QP = (mfxU16)nLastAQ;
						} else {
							encCtrl.FrameType = currentFrameFlag & 0xff;
							encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
						}
						nLastFrameFlag = (mfxU16)currentFrameFlag;
						nLastAQ = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[1];
						pSurfEncIn->Data.TimeStamp = 0;
					} else if (m_nExPrm & MFX_PRM_EX_DEINT_BOB) {
						if (bVppDeintBobFirstFeild) {
							nLastFrameFlag = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
							nLastAQ = m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[1];
							encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
							encCtrl.FrameType = nLastFrameFlag & 0xff;
							pSurfEncIn->Data.TimeStamp = 0;
						} else {
							encCtrl.FrameType = nLastFrameFlag >> 8;
							encCtrl.QP = (mfxU16)nLastAQ;
						}
						bVppDeintBobFirstFeild ^= true;
					} else {
						encCtrl.FrameType |= m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].frameFlag;
						encCtrl.QP = (mfxU16)m_EncThread.m_InputBuf[pSurfEncIn->Data.TimeStamp].AQP[0];
						pSurfEncIn->Data.TimeStamp = 0;
					}
					ptrCtrl = &encCtrl;
				}
				sts = m_pmfxENC->EncodeFrameAsync(ptrCtrl, pSurfEncIn, &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);
				bDeviceBusy = false;

				if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
				{
					bDeviceBusy = true;
					if (MFX_WRN_DEVICE_BUSY == sts)
						Sleep(1); // wait if device is busy
				}
				else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
				{
					sts = MFX_ERR_NONE; // ignore warnings if output is available
					break;
				}
				else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
				{
					sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
					MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
				}
				else
				{
					// get next surface and new task for 2nd bitstream in ViewOutput mode
					MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
					break;
				}
			}
		}

		// MFX_ERR_MORE_DATA is the correct status to exit buffering loop with
		// indicates that there are no more buffered frames
		MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
		// exit in case of other errors
		MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	}

	// loop to get buffered frames from encoder
	while (MFX_ERR_NONE <= sts)
	{
		// get a free task (bit stream and sync point for encoder)
		sts = GetFreeTask(&pCurrentTask);
		MSDK_BREAK_ON_ERROR(sts);

		for (;;)
		{
			sts = m_pmfxENC->EncodeFrameAsync(NULL, NULL, &pCurrentTask->mfxBS, &pCurrentTask->EncSyncP);

			if (MFX_ERR_NONE < sts && !pCurrentTask->EncSyncP) // repeat the call if warning and no output
			{
				if (MFX_WRN_DEVICE_BUSY == sts)
					Sleep(1); // wait if device is busy
			}
			else if (MFX_ERR_NONE < sts && pCurrentTask->EncSyncP)
			{
				sts = MFX_ERR_NONE; // ignore warnings if output is available
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
			{
				sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
				MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
			}
			else
			{
				// get new task for 2nd bitstream in ViewOutput mode
				MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_BITSTREAM);
				break;
			}
		}
		MSDK_BREAK_ON_ERROR(sts);
	}

	// MFX_ERR_MORE_DATA is the correct status to exit buffering loop with
	// indicates that there are no more buffered frames
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	// exit in case of other errors
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	// synchronize all tasks that are left in task pool
	while (MFX_ERR_NONE == sts)
	{
		sts = m_TaskPool.SynchronizeFirstTask();
	}

	// MFX_ERR_NOT_FOUND is the correct status to exit the loop with
	// EncodeFrameAsync and SyncOperation don't return this status
	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_NOT_FOUND);
	// report any errors that occurred in asynchronous part
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	return sts;
}

void CEncodingPipeline::PrintMes(const TCHAR *format, ... ) {
	va_list args;
	va_start(args, format);

	int len = _vsctprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
	TCHAR *buffer = (TCHAR *)malloc(len * sizeof(buffer[0]));
	if (NULL != buffer) {

		_vstprintf_s(buffer, len, format, args); // C4996

#ifdef UNICODE
		DWORD mode = 0;
		bool stderr_write_to_console = 0 != GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode); //stderrの出力先がコンソールかどうか

		char *buffer_char = NULL;
		if (m_pStrLog || !stderr_write_to_console) {
			if (NULL != (buffer_char = (char *)calloc(len * 2, sizeof(buffer_char[0]))))
				WideCharToMultiByte(CP_THREAD_ACP, WC_NO_BEST_FIT_CHARS, buffer, -1, buffer_char, len * 2, NULL, NULL);
		}
		if (buffer_char) {
#else
			char *buffer_char = buffer;
#endif
			if (m_pStrLog) {
				FILE *fp_log = NULL;
				//logはANSI(まあようはShift-JIS)で保存する
				if (0 == _tfopen_s(&fp_log, m_pStrLog, _T("a")) && fp_log) {
					fprintf(fp_log, buffer_char);
					fclose(fp_log);
				}
			}
#ifdef UNICODE
			if (!stderr_write_to_console) //出力先がリダイレクトされるならANSIで
				fprintf(stderr, buffer_char);
			free(buffer_char);
		}
		if (stderr_write_to_console) //出力先がコンソールならWCHARで
#endif
			_ftprintf(stderr, buffer);  
		free(buffer);
	}
}

mfxStatus CEncodingPipeline::CheckCurrentVideoParam()
{
	mfxIMPL impl;
	m_mfxSession.QueryIMPL(&impl);

	mfxFrameInfo SrcPicInfo = m_mfxVppParams.vpp.In;
	mfxFrameInfo DstPicInfo = m_mfxEncParams.mfx.FrameInfo;

	mfxU8 spsbuf[256] = { 0 };
	mfxU8 ppsbuf[256] = { 0 };
	mfxExtCodingOptionSPSPPS spspps;
	MSDK_ZERO_MEMORY(spspps);
	spspps.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
	spspps.Header.BufferSz = sizeof(mfxExtCodingOptionSPSPPS);
	spspps.SPSBuffer = spsbuf;
	spspps.SPSBufSize = sizeof(spsbuf);
	spspps.PPSBuffer = ppsbuf;
	spspps.PPSBufSize = sizeof(ppsbuf);

	mfxExtCodingOption cop;
	MSDK_ZERO_MEMORY(cop);
	cop.Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
	cop.Header.BufferSz = sizeof(mfxExtCodingOption);

	mfxExtCodingOption2 cop2;
	MSDK_ZERO_MEMORY(cop2);
	cop2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
	cop2.Header.BufferSz = sizeof(mfxExtCodingOption2);

	std::vector<mfxExtBuffer *> buf;
	buf.push_back((mfxExtBuffer *)&cop);
	buf.push_back((mfxExtBuffer *)&spspps);
	if (m_bHaswellOrLater)
		buf.push_back((mfxExtBuffer *)&cop2);

	mfxVideoParam videoPrm;
	MSDK_ZERO_MEMORY(videoPrm);
	videoPrm.NumExtParam = (mfxU16)buf.size();
	videoPrm.ExtParam = &buf[0];

	mfxStatus sts = m_pmfxENC->GetVideoParam(&videoPrm);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	sts = m_pFileWriter->SetVideoParam(&videoPrm);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	TCHAR info[4096];
	mfxU32 info_len = 0;

#define PRINT_INFO(fmt, ...) { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, fmt, __VA_ARGS__); }
#define PRINT_INT_AUTO(fmt, i) { if (i) { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, fmt, i); } else { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, (fmt[_tcslen(fmt)-1]=='\n') ? _T("Auto\n") : _T("Auto")); } }
	PRINT_INFO(    _T("based on Intel(R) Media SDK Encoding Sample Version %s\n"), MSDK_SAMPLE_VERSION);
	//PRINT_INFO(    _T("Input Frame Format      %s\n"), ColorFormatToStr(m_pFileReader->m_ColorFormat));
	//PRINT_INFO(    _T("Input Frame Type      %s\n"), list_interlaced[get_cx_index(list_interlaced, SrcPicInfo.PicStruct)].desc);
	PRINT_INFO(    _T("Input Frame Info        %s\n"), m_pFileReader->GetInputMessage());
	sInputCrop inputCrop;
	m_pFileReader->GetInputCropInfo(&inputCrop);
	if (inputCrop.bottom || inputCrop.left || inputCrop.right || inputCrop.up)
		PRINT_INFO(_T("Crop                    %d,%d,%d,%d (%dx%d -> %dx%d)\n"),
			inputCrop.left, inputCrop.up, inputCrop.right, inputCrop.bottom,
			SrcPicInfo.CropW + inputCrop.left + inputCrop.right,
			SrcPicInfo.CropH + inputCrop.up + inputCrop.bottom,
			SrcPicInfo.CropW, SrcPicInfo.CropH);

	if (VppExtMes.size()) {
		const TCHAR *m = _T("VPP Enabled             ");
		size_t len = VppExtMes.length() + 1;
		TCHAR *vpp_mes = (TCHAR*)malloc(len * sizeof(vpp_mes[0]));
		memcpy(vpp_mes, VppExtMes.c_str(), len * sizeof(vpp_mes[0]));
		for (TCHAR *p = vpp_mes, *q; (p = _tcstok_s(p, _T("\n"), &q)) != NULL; ) {
			PRINT_INFO(_T("%s%s\n"), m, p);
			m    = _T("                        ");
			p = NULL;
		}
		free(vpp_mes);
		VppExtMes.clear();
	}
	//if (SrcPicInfo.CropW != DstPicInfo.CropW || SrcPicInfo.CropH != DstPicInfo.CropH)
	//	PRINT_INFO(_T("Resolution              %dx%d -> %dx%d\n"), SrcPicInfo.CropW, SrcPicInfo.CropH, DstPicInfo.CropW, DstPicInfo.CropH);
	PRINT_INFO(    _T("Output Video            %s  %s @ Level %s\n"), CodecIdToStr(videoPrm.mfx.CodecId).c_str(),
		                                             get_profile_list(videoPrm.mfx.CodecId)[get_cx_index(get_profile_list(videoPrm.mfx.CodecId), videoPrm.mfx.CodecProfile)].desc,
		                                             get_level_list(videoPrm.mfx.CodecId)[get_cx_index(get_level_list(videoPrm.mfx.CodecId), videoPrm.mfx.CodecLevel)].desc);
	PRINT_INFO(    _T("                        %dx%d%s %d:%d %0.3ffps (%d/%dfps)%s%s\n"),
	                                                 DstPicInfo.CropW, DstPicInfo.CropH, (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("p") : _T("i"),
													 videoPrm.mfx.FrameInfo.AspectRatioW, videoPrm.mfx.FrameInfo.AspectRatioH,
													 DstPicInfo.FrameRateExtN / (double)DstPicInfo.FrameRateExtD, DstPicInfo.FrameRateExtN, DstPicInfo.FrameRateExtD,
													 (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("") : _T(", "),
													 (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("") : list_interlaced[get_cx_index(list_interlaced, DstPicInfo.PicStruct)].desc);

	PRINT_INFO(    _T("Encode Mode             %s\n"), EncmodeToStr((videoPrm.mfx.RateControlMethod == MFX_RATECONTROL_CQP && (m_nExPrm & MFX_PRM_EX_VQP)) ? MFX_RATECONTROL_VQP : videoPrm.mfx.RateControlMethod));
	if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
		if (m_nExPrm & MFX_PRM_EX_VQP) {
			//PRINT_INFO(_T("VQP params              I:%d  P:%d+  B:%d+  strength:%d  sensitivity:%d\n"), videoPrm.mfx.QPI, videoPrm.mfx.QPP, videoPrm.mfx.QPB, m_SceneChange.getVQPStrength(), m_SceneChange.getVQPSensitivity());
			PRINT_INFO(_T("VQP params              I:%d  P:%d+  B:%d+\n"), videoPrm.mfx.QPI, videoPrm.mfx.QPP, videoPrm.mfx.QPB);
		} else {
			PRINT_INFO(_T("CQP Value               I:%d  P:%d  B:%d\n"), videoPrm.mfx.QPI, videoPrm.mfx.QPP, videoPrm.mfx.QPB);
		}
	} else if (MFX_RATECONTROL_LA     == m_mfxEncParams.mfx.RateControlMethod
		    || MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod) {
		PRINT_INFO(_T("Lookahead Depth         %d frames\n"), cop2.LookAheadDepth);
		if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
			PRINT_INFO(_T("Lookahead Quality         %s\n"), list_lookahead_ds[get_cx_index(list_lookahead_ds, cop2.LookAheadDS)].desc);
		}
	} else {
		PRINT_INFO(_T("Bitrate                 %d kbps\n"), videoPrm.mfx.TargetKbps);
		if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR) {
			//PRINT_INFO(_T("AVBR Accuracy range\t%.01lf%%"), m_mfxEncParams.mfx.Accuracy / 10.0);
			PRINT_INFO(_T("AVBR Convergence        %d frames unit\n"), videoPrm.mfx.Convergence * 100);
		} else {
			PRINT_INFO(_T("Max Bitrate             "));
			PRINT_INT_AUTO(_T("%d kbps\n"), videoPrm.mfx.MaxKbps);
		}
	}
	PRINT_INFO(    _T("Target usage            %s\n"), TargetUsageToStr(videoPrm.mfx.TargetUsage));
	if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
		PRINT_INFO(_T("Trellis                 %s\n"), list_avc_trellis[get_cx_index(list_avc_trellis_for_options, cop2.Trellis)].desc);
	}
	if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
		PRINT_INFO(_T("Ext. Bitrate Control    %s%s%s\n"),
			(cop2.MBBRC  != MFX_CODINGOPTION_ON && cop2.ExtBRC != MFX_CODINGOPTION_ON) ? _T("disabled") : _T(""),
			(cop2.MBBRC  == MFX_CODINGOPTION_ON) ? _T("PerMBRateControl ") : _T(""),
			(cop2.ExtBRC == MFX_CODINGOPTION_ON) ? _T("ExtBRC ") : _T(""));
	}

	if (videoPrm.mfx.CodecId == MFX_CODEC_AVC && !Check_HWUsed(impl)) {
		PRINT_INFO(    _T("CABAC                   %s\n"), (cop.CAVLC == MFX_CODINGOPTION_ON) ? _T("off") : _T("on"));
		PRINT_INFO(    _T("RDO                     %s\n"), (cop.RateDistortionOpt == MFX_CODINGOPTION_ON) ? _T("on") : _T("off"));
		if ((cop.MVSearchWindow.x | cop.MVSearchWindow.y) == 0) {
			PRINT_INFO(    _T("mv search               precision: %s\n"), list_mv_presicion[get_cx_index(list_mv_presicion, cop.MVPrecision)].desc);
		} else {
			PRINT_INFO(    _T("mv search               precision: %s, window size:%dx%d\n"), list_mv_presicion[get_cx_index(list_mv_presicion, cop.MVPrecision)].desc, cop.MVSearchWindow.x, cop.MVSearchWindow.y);
		}
		PRINT_INFO(    _T("min pred block size     inter: %s   intra: %s\n"), list_pred_block_size[get_cx_index(list_pred_block_size, cop.InterPredBlockSize)].desc, list_pred_block_size[get_cx_index(list_pred_block_size, cop.IntraPredBlockSize)].desc);
	}
	PRINT_INFO(    _T("Ref frames              "));
	PRINT_INT_AUTO(_T("%d frames\n"), videoPrm.mfx.NumRefFrame);

	PRINT_INFO(    _T("Bframe Settings         "));
	switch (videoPrm.mfx.GopRefDist) {
		case 0:  PRINT_INFO(_T("Auto\n")); break;
		case 1:  PRINT_INFO(_T("none\n")); break;
		default: PRINT_INFO(_T("%d frame%s\n"), videoPrm.mfx.GopRefDist - 1, (videoPrm.mfx.GopRefDist > 2) ? _T("s") : _T("")); break;
	}

	//PRINT_INFO(    _T("Idr Interval          %d\n"), videoPrm.mfx.IdrInterval);
	PRINT_INFO(    _T("Max GOP Length          "));
	PRINT_INT_AUTO(_T("%d frames\n"), min(videoPrm.mfx.GopPicSize, m_SceneChange.getMaxGOPLen()));
	PRINT_INFO(    _T("Scene Change Detection  %s\n"), m_SceneChange.isInitialized() ? _T("on") : _T("off"));
	if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
		PRINT_INFO(    _T("GOP Structure           "));
		bool adaptiveIOn = (MFX_CODINGOPTION_ON == cop2.AdaptiveI);
		bool adaptiveBOn = (MFX_CODINGOPTION_ON == cop2.AdaptiveB);
		if (!adaptiveIOn && !adaptiveBOn) {
			PRINT_INFO(_T("fixed\n"))
		} else {
			PRINT_INFO(_T("Adaptive %s%s insert\n"),
				(adaptiveIOn) ? _T("I") : _T(""),
				(adaptiveIOn && adaptiveBOn) ? _T(",") : _T(""),
				(adaptiveBOn) ? _T("B") : _T(""));
		}
	}
	PRINT_INFO(    _T("Slices                  %d\n"), videoPrm.mfx.NumSlice);
	//if (   MFX_CODINGOPTION_ON == cop.AUDelimiter
	//	|| MFX_CODINGOPTION_ON == cop.PicTimingSEI
	//	|| MFX_CODINGOPTION_ON == cop.SingleSeiNalUnit) {
	//	PRINT_INFO(    _T("Output Bitstream Info   %s%s%s\n"),
	//		(MFX_CODINGOPTION_ON == cop.AUDelimiter) ? _T("aud ") : _T(""),
	//		(MFX_CODINGOPTION_ON == cop.PicTimingSEI) ? _T("pic_struct ") : _T(""),
	//		(MFX_CODINGOPTION_ON == cop.SingleSeiNalUnit) ? _T("SingleSEI ") : _T(""));
	//}

	//PRINT_INFO(_T("Source picture:"));
	//PRINT_INFO(_T("\tResolution\t%dx%d"), SrcPicInfo.Width, SrcPicInfo.Height);
	//PRINT_INFO(_T("\tCrop X,Y,W,H\t%d,%d,%d,%d"), SrcPicInfo.CropX, SrcPicInfo.CropY, SrcPicInfo.CropW, SrcPicInfo.CropH);

	//PRINT_INFO(_T("Destination picture:"));
	//PRINT_INFO(_T("\tResolution\t%dx%d"), DstPicInfo.Width, DstPicInfo.Height);
	//PRINT_INFO(_T("\tCrop X,Y,W,H\t%d,%d,%d,%d"), DstPicInfo.CropX, DstPicInfo.CropY, DstPicInfo.CropW, DstPicInfo.CropH);


	PRINT_INFO(    _T("Memory type             %s\n"), MemTypeToStr(m_memType));
	PRINT_INFO(    _T("Input Buffer Size       %d frames\n"), m_EncThread.m_nFrameBuffer);
	//PRINT_INFO(    _T("Threads               %d\n"), videoPrm.mfx.NumThread);

	if (Check_HWUsed(impl)) {
		static const TCHAR * const NUM_APPENDIX[] = { _T("st"), _T("nd"), _T("rd"), _T("th")};
		mfxU32 iGPUID = MSDKAdapter::GetNumber(m_mfxSession);
		PRINT_INFO(_T("Intel iGPU ID           %d%s GPU\n"), iGPUID + 1, NUM_APPENDIX[clamp(iGPUID, 0, _countof(NUM_APPENDIX) - 1)]);
	}
	PRINT_INFO(    _T("Media SDK impl          %s, API v%d.%d\n"), (Check_HWUsed(impl)) ? _T("QuickSyncVideo (hardware encoder)") : _T("software encoder"), m_mfxVer.Major, m_mfxVer.Minor);
	PRINT_INFO(_T("\n"));

	PrintMes(info);

	return MFX_ERR_NONE;
#undef PRINT_INFO
#undef PRINT_INT_AUTO
}

