// Copyright 2014 GSI, Inc. All rights reserved.
//
//
//

#ifndef __DDS__ConnectionManager__
#define __DDS__ConnectionManager__
// DDS
#include "ConnectionManagerImpl.h"
#include "AgentChannel.h"
#include "UIChannelInfo.h"
#include "Options.h"
#include "Topology.h"
#include "SSHScheduler.h"
// STD
#include <mutex>

namespace dds
{
    class CConnectionManager : public CConnectionManagerImpl<CAgentChannel, CConnectionManager>
    {
      public:
        CConnectionManager(const SOptions_t& _options,
                           boost::asio::io_service& _io_service,
                           boost::asio::ip::tcp::endpoint& _endpoint);

        ~CConnectionManager();

      public:
        void newClientCreated(CAgentChannel::connectionPtr_t _newClient);
        void _start()
        {
        }
        void _stop()
        {
        }
        void _createInfoFile(size_t _port) const;
        void _deleteInfoFile() const;

      private:
        bool on_cmdGET_AGENTS_INFO(SCommandAttachmentImpl<cmdGET_AGENTS_INFO>::ptr_t _attachment,
                                   CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdGET_LOG(SCommandAttachmentImpl<cmdGET_LOG>::ptr_t _attachment,
                           CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdBINARY_ATTACHMENT_RECEIVED(SCommandAttachmentImpl<cmdBINARY_ATTACHMENT_RECEIVED>::ptr_t _attachment,
                                              CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdSUBMIT(SCommandAttachmentImpl<cmdSUBMIT>::ptr_t _attachment,
                          CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdACTIVATE_AGENT(SCommandAttachmentImpl<cmdACTIVATE_AGENT>::ptr_t _attachment,
                                  CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdTRANSPORT_TEST(SCommandAttachmentImpl<cmdTRANSPORT_TEST>::ptr_t _attachment,
                                  CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdSIMPLE_MSG(SCommandAttachmentImpl<cmdSIMPLE_MSG>::ptr_t _attachment,
                              CAgentChannel::weakConnectionPtr_t _channel);
        bool on_cmdUPDATE_KEY(SCommandAttachmentImpl<cmdUPDATE_KEY>::ptr_t _attachment,
                              CAgentChannel::weakConnectionPtr_t _channel);

        CGetLogChannelInfo m_getLog;
        CTestChannelInfo m_transportTest;
        CActivateAgentsChannelInfo m_ActivateAgents;
        CTopology m_topo;
        CSSHScheduler m_scheduler;

        // TODO: This is temporary storage only. Store this information as a part of scheduler.
        typedef std::map<uint64_t, CAgentChannel::weakConnectionPtr_t> TaskIDToAgentChannelMap_t;
        TaskIDToAgentChannelMap_t m_taskIDToAgentChannelMap;
    };
}
#endif /* defined(__DDS__ConnectionManager__) */
