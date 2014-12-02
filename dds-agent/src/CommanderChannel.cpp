// Copyright 2014 GSI, Inc. All rights reserved.
//
//
//

// DDS
#include "version.h"
#include "CommanderChannel.h"
#include "BOOST_FILESYSTEM.h"
#include "KeyValueGuard.h"
// MiscCommon
#include "FindCfgFile.h"
// BOOST
#include <boost/crc.hpp>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-register"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#pragma clang diagnostic pop
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

using namespace MiscCommon;
using namespace dds;
using namespace std;
namespace fs = boost::filesystem;

CCommanderChannel::CCommanderChannel(boost::asio::io_service& _service)
    : CConnectionImpl<CCommanderChannel>(_service)
    , m_isHandShakeOK(false)
{
}

bool CCommanderChannel::on_cmdREPLY_HANDSHAKE_OK(SCommandAttachmentImpl<cmdREPLY_HANDSHAKE_OK>::ptr_t _attachment)
{
    m_isHandShakeOK = true;

    return true;
}

bool CCommanderChannel::on_cmdSIMPLE_MSG(SCommandAttachmentImpl<cmdSIMPLE_MSG>::ptr_t _attachment)
{
    switch (_attachment->m_srcCommand)
    {
        case cmdTRANSPORT_TEST:
        {
            pushMsg<cmdSIMPLE_MSG>(*_attachment);
            return true;
        }
        case cmdUPDATE_KEY:
            return false; // let connection manager forward this info to UI channels

        default:
            LOG(debug) << "Received command cmdSIMPLE_MSG does not have a listener";
            return true;
    }

    return true;
}

bool CCommanderChannel::on_cmdGET_HOST_INFO(SCommandAttachmentImpl<cmdGET_HOST_INFO>::ptr_t _attachment)
{
    // pid
    pid_t pid = getpid();

    // UI port number
    size_t nPort(0);
    try
    {
        // Read server info file
        const string sSrvCfg(CUserDefaults::instance().getAgentInfoFileLocation());
        LOG(info) << "Reading server info from: " << sSrvCfg;
        if (sSrvCfg.empty())
            throw runtime_error("Cannot find agent info file.");

        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(sSrvCfg, pt);
        nPort = pt.get<size_t>("agent.port");
    }
    catch (...)
    {
    }

    SHostInfoCmd cmd;
    get_cuser_name(&cmd.m_username);
    get_hostname(&cmd.m_host);
    cmd.m_version = PROJECT_VERSION_STRING;
    cmd.m_DDSPath = CUserDefaults::getDDSPath();
    cmd.m_agentPort = nPort;
    cmd.m_agentPid = pid;

    pushMsg<cmdREPLY_HOST_INFO>(cmd);
    return true;
}

bool CCommanderChannel::on_cmdDISCONNECT(SCommandAttachmentImpl<cmdDISCONNECT>::ptr_t _attachment)
{
    LOG(info) << "The Agent [" << m_id << "] disconnected... Bye";
    stop();

    return true;
}

bool CCommanderChannel::on_cmdSHUTDOWN(SCommandAttachmentImpl<cmdSHUTDOWN>::ptr_t _attachment)
{
    deleteAgentUUIDFile();
    LOG(info) << "The Agent [" << m_id << "] exited.";
    stop();

    // return false to let connection manager to catch this message as weel
    return false;
}

bool CCommanderChannel::on_cmdBINARY_ATTACHMENT_RECEIVED(
    SCommandAttachmentImpl<cmdBINARY_ATTACHMENT_RECEIVED>::ptr_t _attachment)
{
    LOG(debug) << "Received command cmdBINARY_ATTACHMENT_RECEIVED";

    switch (_attachment->m_srcCommand)
    {
        case cmdTRANSPORT_TEST:
        {
            // Remove received file
            boost::filesystem::remove(_attachment->m_receivedFilePath);

            pushMsg<cmdBINARY_ATTACHMENT_RECEIVED>(*_attachment);
            return true;
        }
        case cmdASSIGN_USER_TASK:
        {
            boost::filesystem::path destFilePath(CUserDefaults::instance().getDDSPath());
            destFilePath /= _attachment->m_requestedFileName;
            boost::filesystem::rename(_attachment->m_receivedFilePath, destFilePath);
            LOG(info) << "Received user executable to execute: " << destFilePath.generic_string();
        }
        default:
            LOG(debug) << "Received command cmdBINARY_ATTACHMENT_RECEIVED does not have a listener";
            return true;
    }

    return true;
}

bool CCommanderChannel::on_cmdGET_UUID(SCommandAttachmentImpl<cmdGET_UUID>::ptr_t _attachment)
{
    // If file exist return uuid from file.
    // If file does not exist than return uuid_nil.

    const string sAgentUUIDFile(CUserDefaults::instance().getAgentUUIDFile());
    if (file_exists(sAgentUUIDFile))
    {
        readAgentUUIDFile();
    }
    else
    {
        m_id = boost::uuids::nil_uuid();
    }

    SUUIDCmd msg_cmd;
    msg_cmd.m_id = m_id;
    pushMsg<cmdREPLY_UUID>(msg_cmd);

    return true;
}

bool CCommanderChannel::on_cmdSET_UUID(SCommandAttachmentImpl<cmdSET_UUID>::ptr_t _attachment)
{
    LOG(info) << "cmdSET_UUID attachment [" << *_attachment << "] from " << remoteEndIDString();

    m_id = _attachment->m_id;

    createAgentUUIDFile();

    return true;
}

bool CCommanderChannel::on_cmdGET_LOG(SCommandAttachmentImpl<cmdGET_LOG>::ptr_t _attachment)
{
    try
    {
        string logDir(CUserDefaults::getDDSPath());

        string hostname;
        get_hostname(&hostname);

        std::time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());
        struct std::tm* ptm = std::localtime(&now);
        char buffer[20];
        std::strftime(buffer, 20, "%Y-%m-%d-%H-%M-%S", ptm);

        stringstream ss;
        // We do not use put_time for the moment as gcc4.9 does not support it.
        // ss << std::put_time(ptm, "%Y-%m-%d-%H-%M-%S") << "_" << hostname << "_" << m_id;
        ss << buffer << "_" << hostname << "_" << m_id;

        string archiveName(ss.str());

        fs::path archiveDir(logDir + archiveName);
        if (!fs::exists(archiveDir) && !fs::create_directory(archiveDir))
        {
            string msg("Could not create directory: " + archiveDir.string());
            pushMsg<cmdSIMPLE_MSG>(SSimpleMsgCmd(msg, error, cmdGET_LOG));
            return true;
        }

        vector<fs::path> logFiles;
        BOOSTHelper::get_files_by_extension(logDir, ".log", logFiles);

        for (const auto& v : logFiles)
        {
            fs::path dest(archiveDir.string() + "/" + v.filename().string());
            if (fs::exists(dest) && !fs::is_directory(dest))
                fs::remove(dest);
            fs::copy(v, dest);
        }

        CFindCfgFile<string> cfg;
        cfg.SetOrder("/usr/bin/tar")("/usr/local/bin/tar")("/opt/local/bin/tar")("/bin/tar");
        string tarPath;
        cfg.GetCfg(&tarPath);

        string archiveDirName = logDir + archiveName;
        string archiveFileName = archiveDirName + ".tar.gz";
        stringstream ssCmd;
        ssCmd << tarPath << " czf " << archiveFileName << " -C" << logDir << " " << archiveName;
        string output;
        do_execv(ssCmd.str(), 60, &output);

        string fileName(archiveName);
        fileName += ".tar.gz";

        pushBinaryAttachmentCmd(archiveFileName, fileName, cmdGET_LOG);

        fs::remove(archiveFileName);
        fs::remove_all(archiveDirName);
    }
    catch (exception& e)
    {
        LOG(error) << e.what();
        pushMsg<cmdSIMPLE_MSG>(SSimpleMsgCmd(e.what(), error, cmdGET_LOG));
    }

    return true;
}

void CCommanderChannel::readAgentUUIDFile()
{
    const string sAgentUUIDFile(CUserDefaults::getAgentUUIDFile());
    LOG(info) << "Reading an agent UUID file: " << sAgentUUIDFile;
    ifstream f(sAgentUUIDFile.c_str());
    if (!f.is_open() || !f.good())
    {
        string msg("Could not open an agent UUID file: ");
        msg += sAgentUUIDFile;
        throw runtime_error(msg);
    }
    f >> m_id;
}

void CCommanderChannel::createAgentUUIDFile() const
{
    const string sAgentUUIDFile(CUserDefaults::getAgentUUIDFile());
    LOG(MiscCommon::info) << "Creating an agent UUID file: " << sAgentUUIDFile;
    ofstream f(sAgentUUIDFile.c_str());
    if (!f.is_open() || !f.good())
    {
        string msg("Could not open an agent UUID file: ");
        msg += sAgentUUIDFile;
        throw runtime_error(msg);
    }

    f << m_id;
}

void CCommanderChannel::deleteAgentUUIDFile() const
{
    const string sAgentUUIDFile(CUserDefaults::getAgentUUIDFile());
    if (sAgentUUIDFile.empty())
        return;

    // TODO: check error code
    unlink(sAgentUUIDFile.c_str());
}

void CCommanderChannel::onRemoteEndDissconnected()
{
    sendYourself<cmdSHUTDOWN>();
}

bool CCommanderChannel::on_cmdASSIGN_USER_TASK(SCommandAttachmentImpl<cmdASSIGN_USER_TASK>::ptr_t _attachment)
{
    LOG(info) << "Recieved a user task assigment. " << *_attachment;
    m_sUsrExe = _attachment->m_sExeFile;
    m_sTaskId = _attachment->m_sID;
    return true;
}

bool CCommanderChannel::on_cmdACTIVATE_AGENT(SCommandAttachmentImpl<cmdACTIVATE_AGENT>::ptr_t _attachment)
{
    string sUsrExe(m_sUsrExe);
    smart_path(&sUsrExe);

    if (sUsrExe.empty())
    {
        LOG(info) << "Recieved activation command. Ignoring the command, since no task is assigned.";
        // Send response back to server
        SSimpleMsgCmd cmd;
        cmd.m_sMsg = "No task is assigned. Activation is ignored.";
        cmd.m_msgSeverity = info;
        cmd.m_srcCommand = cmdACTIVATE_AGENT;
        pushMsg<cmdSIMPLE_MSG>(cmd);
        return true;
    }

    StringVector_t params;
    string output;
    pid_t pidUsrTask(0);

    try
    {
        // set task's environment
        LOG(info) << "Setting up task's environment: "
                  << "DDS_TASK_ID:" << m_sTaskId;
        if (::setenv("DDS_TASK_ID", m_sTaskId.c_str(), 1) == -1)
            throw MiscCommon::system_error("Failed to set up $DDS_TASK_ID");

        // execute the task
        LOG(info) << "Executing user task: " << sUsrExe;
        pidUsrTask = do_execv(sUsrExe, 0, &output);
    }
    catch (exception& e)
    {
        LOG(error) << e.what();

        // Send response back to server
        SSimpleMsgCmd cmd;
        cmd.m_sMsg = e.what();
        cmd.m_msgSeverity = error;
        cmd.m_srcCommand = cmdACTIVATE_AGENT;
        pushMsg<cmdSIMPLE_MSG>(cmd);
    }

    stringstream ss;
    ss << "User task (pid:" << pidUsrTask << ") is activated.";
    LOG(info) << ss.str();

    m_onNewUserTaskCallback(pidUsrTask);

    // Send response back to server
    SSimpleMsgCmd cmd;
    cmd.m_sMsg = ss.str();
    cmd.m_msgSeverity = info;
    cmd.m_srcCommand = cmdACTIVATE_AGENT;
    pushMsg<cmdSIMPLE_MSG>(cmd);

    return true;
}

bool CCommanderChannel::on_cmdUPDATE_KEY(SCommandAttachmentImpl<cmdUPDATE_KEY>::ptr_t _attachment)
{
    try
    {
        LOG(info) << "Recieved a key update notifications: " << *_attachment;
        CKeyValueGuard::instance().putValue(_attachment->m_sKey, _attachment->m_sValue);
    }
    catch (exception& e)
    {
        LOG(error) << e.what();

        // Send response back to server
        SSimpleMsgCmd cmd;
        cmd.m_sMsg = e.what();
        cmd.m_msgSeverity = error;
        cmd.m_srcCommand = cmdUPDATE_KEY;
        pushMsg<cmdSIMPLE_MSG>(cmd);
    }

    // give a chance to others to recive update nitifications
    return false;
}

void CCommanderChannel::updateKey(const string& _key, const string& _value)
{
    SUpdateKeyCmd cmd;
    // Update key name with the task id
    cmd.m_sKey = _key + "." + m_sTaskId;
    cmd.m_sValue = _value;
    LOG(debug) << "Sending commander a notification about the key update (key:value) " << cmd.m_sKey << ":"
               << cmd.m_sValue;
    pushMsg<cmdUPDATE_KEY>(cmd);
}
