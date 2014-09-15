// Copyright 2014 GSI, Inc. All rights reserved.
//
//
//

// DDS
#include "SubmitChannel.h"

using namespace MiscCommon;
using namespace dds;
using namespace std;

void CSubmitChannel::setTopoFile(const string& _val)
{
    m_sTopoFile = _val;
}

void CSubmitChannel::setSSHCfgFile(const string& _val)
{
    m_sSSHCfgFile = _val;
}

void CSubmitChannel::setRMSTypeCode(const SSubmitCmd::ERmsType& _val)
{
    m_RMS = _val;
}

bool CSubmitChannel::on_cmdREPLY_HANDSHAKE_OK(SCommandAttachmentImpl<cmdREPLY_HANDSHAKE_OK>::ptr_t /*_attachment*/)
{
    m_isHandShakeOK = true;

    if (!m_sTopoFile.empty() && SSubmitCmd::UNKNOWN != m_RMS)
    {
        // Create the command's attachment
        SSubmitCmd cmd;
        cmd.m_sTopoFile = m_sTopoFile;
        cmd.m_nRMSTypeCode = m_RMS;
        cmd.m_sSSHCfgFile = m_sSSHCfgFile;
        pushMsg<cmdSUBMIT>(cmd);
    }
    // Check wheather we need to start distribuiting tasks
    if (m_bSendStart)
    {
        pushMsg<cmdACTIVATE_AGENT>();
    }

    return true;
}

bool CSubmitChannel::on_cmdSIMPLE_MSG(SCommandAttachmentImpl<cmdSIMPLE_MSG>::ptr_t _attachment)
{
    if (!_attachment->m_sMsg.empty())
        LOG((_attachment->m_msgSeverity == fatal || _attachment->m_msgSeverity == error) ? log_stderr : log_stdout)
            << "Server reports: " << _attachment->m_sMsg;

    // stop communication if a fatal error is recieved
    if (_attachment->m_msgSeverity == fatal)
        stop();
    return true;
}

bool CSubmitChannel::on_cmdSHUTDOWN(SCommandAttachmentImpl<cmdSHUTDOWN>::ptr_t /*_attachment*/)
{
    // Close communication channel
    stop();
    return true;
}
