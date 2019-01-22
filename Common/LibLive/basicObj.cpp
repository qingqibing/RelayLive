#include "stdafx.h"
#include "basicObj.h"
#include "ts.h"
#include "flv.h"
#include "mp4.h"
#include "h264.h"

CBasicObj::CBasicObj()
	: m_pTs(nullptr)
    , m_pFlv(nullptr)
    , m_pCallBack(nullptr)
{
    m_pH264          = new CH264(this);
    m_pTs            = new CTS(this);
    m_pFlv           = new CFlv(this);
    m_pMp4           = new CMP4(this);
}

CBasicObj::~CBasicObj(void)
{
    SAFE_DELETE(m_pH264);
    SAFE_DELETE(m_pTs);
    SAFE_DELETE(m_pFlv);
    SAFE_DELETE(m_pMp4);
}

void CBasicObj::H264SpsCb(uint32_t nWidth, uint32_t nHeight, double fFps)
{
    if(nullptr != m_pFlv)
    {
        CFlv* flv = (CFlv*)m_pFlv;
        flv->SetSps(nWidth,nHeight,fFps);
    }
    if (nullptr != m_pMp4)
    {
        CMP4* mp4 = (CMP4*)m_pMp4;
        mp4->SetSps(nWidth,nHeight,fFps);
    }
}

void CBasicObj::FlvCb(FLV_FRAG_TYPE eType, char* pBuff, int nBuffSize)
{
    CHECK_POINT_VOID(m_pCallBack);
    m_pCallBack->push_flv_frame(eType, pBuff, nBuffSize);
}

void CBasicObj::Mp4Cb(MP4_FRAG_TYPE eType, char* pBuff, int nBuffSize)
{
    CHECK_POINT_VOID(m_pCallBack);
    m_pCallBack->push_mp4_stream(eType, pBuff, nBuffSize);
}

void CBasicObj::TsCb(char* pBuff, int nBuffSize)
{
    CHECK_POINT_VOID(m_pCallBack);
    m_pCallBack->push_ts_stream(pBuff, nBuffSize);
}

void CBasicObj::H264Cb(char* pBuff, int nBuffSize)
{
    CHECK_POINT_VOID(m_pCallBack);
	if (m_pCallBack->m_bH264)
		m_pCallBack->push_h264_stream(pBuff, nBuffSize);
}
