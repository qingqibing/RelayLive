#include "stdafx.h"
#include "rtspObj.h"
#include "ts.h"
#include "flv.h"
#include "mp4.h"
#include "h264.h"

IlibLive* IlibLive::CreateRtspObj(string rtsp)
{
    return new CRtspObj(rtsp);
}

CRtspObj::CRtspObj(std::string rtsp)
	: m_strRtspAddr(rtsp)
{
}

CRtspObj::~CRtspObj(void)
{
    
    Sleep(2000);
}

void CRtspObj::StartListen()
{

    m_bRun = true;

}


void CRtspObj::ESParseCb(char* pBuff, long nLen/*, uint8_t nNalType*/)
{
    //nal_unit_header4* nalu = (nal_unit_header4*)pBuff;
    //Log::debug("CLiveInstance::ESParseCb nlen:%ld, buff:%02X %02X %02X %02X %02X", nLen,pBuff[0],pBuff[1],pBuff[2],pBuff[3],pBuff[4]);
    CHECK_POINT_VOID(pBuff);
    CH264* pH264 = (CH264*)m_pH264;
    //m_pNaluBuff = pBuff;
    //m_nNaluLen  = nLen;
    pH264->InputBuffer(pBuff, nLen);
    NalType m_nalu_type = pH264->NaluType();
    uint32_t nDataLen = 0;
    char* pData = pH264->DataBuff(nDataLen);

    CHECK_POINT_VOID(m_pCallBack);

    //需要回调Flv
    if(m_pCallBack->m_bFlv && nullptr != m_pFlv)
    {
        CFlv* flv = (CFlv*)m_pFlv;
        flv->InputBuffer(m_nalu_type, pData, nDataLen);
    }

    //需要回调mp4
    if (m_pCallBack->m_bMp4 && nullptr != m_pMp4)
    {
        CMP4* mp4 = (CMP4*)m_pMp4;
        mp4->InputBuffer(m_nalu_type, pData, nDataLen);
    }
}
