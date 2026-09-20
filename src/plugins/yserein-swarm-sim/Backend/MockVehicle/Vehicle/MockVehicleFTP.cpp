//
// Created by Hoppz on 25-7-2.
//

#include "MockVehicleFTP.h"

#include <QDir>
#include <QtEndian>

#include "ZSMLoggingCategory.h"
#include "MockVehicle.h"
#include "ZSMStatic.h"

ZSM_LOGGING_CATEGORY(MockVehicleFTPLog, "Backend.MockVehicle.MockVehicleFTP")

namespace {

quint32 updateCrc32(quint32 state, const QByteArray& bytes)
{
    for (const char byte : bytes) {
        state ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            state = (state >> 1) ^ ((state & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return state;
}

} // namespace

MockVehicleFTP::MockVehicleFTP(uint16_t systemIdServer, uint16_t componentIdServer, MockVehicle *vehicle)
    : QObject(vehicle)
    , _systemIdServer(systemIdServer)
    , _componentIdServer(componentIdServer)
    , _vehicle(vehicle)
{
    // zInfo(MockVehicleFTPLog) << "create";
}

MockVehicleFTP::~MockVehicleFTP()
{
    // zInfo(MockVehicleFTPLog) << "destroy";
}

void MockVehicleFTP::mavlinkMessageReceived(const mavlink_message_t &message)
{
    if ( message.msgid != MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL ) return ;
    zDebug(MockVehicleFTPLog) << "mavlinkMessageReceived";

    mavlink_file_transfer_protocol_t requestFTP{};
    mavlink_msg_file_transfer_protocol_decode(&message, &requestFTP);

    if (requestFTP.target_system != _systemIdServer) {
        return;
    }

    MavlinkFTP::Request *request = reinterpret_cast<MavlinkFTP::Request*>(&requestFTP.payload[0]);

    // kCmdOpenFileRO and kCmdResetSessions don't support retry so we can't drop those
    if (_randomDropsEnabled && (request->hdr.opcode != MavlinkFTP::kCmdOpenFileRO) && (request->hdr.opcode != MavlinkFTP::kCmdResetSessions)) {
        if ((rand() % 5) == 0) {
            zDebug(MockVehicleFTPLog) << " Random drop of incoming packet";
            return;
        }
    }

    if (_lastReplyValid && (request->hdr.seqNumber == (_lastReplySequence - 1))) {
        // This is the same request as the one we replied to last. It means the (n)ack got lost, and the GCS
        // resent the request
        zDebug(MockVehicleFTPLog) << "resending response";
        _vehicle->sendMessage(_lastReply);
        return;
    }

    const uint16_t incomingSeqNumber = request->hdr.seqNumber;
    const uint16_t outgoingSeqNumber = _nextSeqNumber(incomingSeqNumber);

    if ((request->hdr.opcode != MavlinkFTP::kCmdResetSessions) && (request->hdr.opcode != MavlinkFTP::kCmdTerminateSession)) {
        if (_errMode == errModeNoResponse) {
            // Don't respond to any requests, this shold cause the client to eventually timeout waiting for the ack
            return;
        }

        if (_errMode == errModeNakResponse) {
            // Nak all requests, the actual error send back doesn't really matter as long as it's an error
            _sendNak(message.sysid, message.compid, MavlinkFTP::kErrFail, outgoingSeqNumber, static_cast<MavlinkFTP::OpCode_t>(request->hdr.opcode));
            return;
        }
    }

    MavlinkFTP::Request ackResponse{};
    switch (request->hdr.opcode) {
    case MavlinkFTP::kCmdNone:
        // ignored, always acked
        ackResponse.hdr.opcode = MavlinkFTP::kRspAck;
        ackResponse.hdr.session = 0;
        ackResponse.hdr.size = 0;
        _sendResponse(message.sysid, message.compid, &ackResponse, outgoingSeqNumber);
        break;
    case MavlinkFTP::kCmdListDirectory:
        _listCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdOpenFileRO:
        _openCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdReadFile:
        _readCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdBurstReadFile:
        _burstReadCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdTerminateSession:
        _terminateCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdResetSessions:
        _resetCommand(message.sysid, message.compid, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdWriteFile:
        _writeCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdCreateFile:
        _createCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    case MavlinkFTP::kCmdCalcFileCRC32:
        _calcFileCrcCommand(message.sysid, message.compid, request, incomingSeqNumber);
        break;
    default:
        // nack for all NYI opcodes
        _sendNak(message.sysid, message.compid, MavlinkFTP::kErrUnknownCommand, outgoingSeqNumber, static_cast<MavlinkFTP::OpCode_t>(request->hdr.opcode));
        break;
    }
}

void MockVehicleFTP::_sendAck(uint16_t targetSystemId, uint8_t targetComponentId, uint16_t seqNumber,
    MavlinkFTP::OpCode_t reqOpcode)
{
    MavlinkFTP::Request ackResponse{};

    ackResponse.hdr.opcode = MavlinkFTP::kRspAck;
    ackResponse.hdr.req_opcode = reqOpcode;
    ackResponse.hdr.session = _sessionId;
    ackResponse.hdr.size = 0;

    _sendResponse(targetSystemId, targetComponentId, &ackResponse, seqNumber);
}

void MockVehicleFTP::_sendNak(uint16_t targetSystemId, uint8_t targetComponentId, MavlinkFTP::ErrorCode_t error,
    uint16_t seqNumber, MavlinkFTP::OpCode_t reqOpcode)
{
    MavlinkFTP::Request nakResponse{};

    nakResponse.hdr.opcode = MavlinkFTP::kRspNak;
    nakResponse.hdr.req_opcode = reqOpcode;
    nakResponse.hdr.session = _sessionId;
    nakResponse.hdr.size = 1;
    nakResponse.data[0] = error;

    _sendResponse(targetSystemId, targetComponentId, &nakResponse, seqNumber);
}

void MockVehicleFTP::_sendNakErrno(uint16_t targetSystemId, uint8_t targetComponentId, uint8_t nakErrno,
    uint16_t seqNumber, MavlinkFTP::OpCode_t reqOpcode)
{
    MavlinkFTP::Request nakResponse{};

    nakResponse.hdr.opcode = MavlinkFTP::kRspNak;
    nakResponse.hdr.req_opcode = reqOpcode;
    nakResponse.hdr.session = _sessionId;
    nakResponse.hdr.size = 2;
    nakResponse.data[0] = MavlinkFTP::kErrFailErrno;
    nakResponse.data[1] = nakErrno;

    _sendResponse(targetSystemId, targetComponentId, &nakResponse, seqNumber);
}

void MockVehicleFTP::_sendResponse(uint16_t targetSystemId, uint8_t targetComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    request->hdr.seqNumber = seqNumber;
    _lastReplySequence = seqNumber;
    _lastReplyValid = true;

    (void) mavlink_msg_file_transfer_protocol_pack_chan(
        _systemIdServer,                    // System ID
        _componentIdServer,                 // Component ID
        ZSM_MAVLINK_COMM,
        &_lastReply,                        // Mavlink Message to pack into
        0,                                  // Target network
        targetSystemId,
        targetComponentId,
        reinterpret_cast<uint8_t*>(request) // Payload
    );

    // kCmdOpenFileRO and kCmdResetSessions don't support retry so we can't drop those
    if (_randomDropsEnabled && (request->hdr.req_opcode != MavlinkFTP::kCmdOpenFileRO) && (request->hdr.req_opcode != MavlinkFTP::kCmdResetSessions)) {
        if ((rand() % 5) == 0) {
            zDebug(MockVehicleFTPLog()) << "Random drop of outgoing packet";
            return;
        }
    }

    _vehicle->sendMessage(_lastReply);
}

void MockVehicleFTP::_listCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    MavlinkFTP::Request ackResponse{};
    ensureNullTemination(request);

    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    // We only support root path
    const QString path = reinterpret_cast<char*>(&request->data[0]);
    if (!path.isEmpty() && path != "/") {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFail, outgoingSeqNumber, MavlinkFTP::kCmdListDirectory);
        return;
    }

    if (request->hdr.offset > 0) {
        if (_errMode == errModeNakSecondResponse) {
            // Nak error all subsequent requests
            _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFail, outgoingSeqNumber, MavlinkFTP::kCmdListDirectory);
            return;
        }

        if (_errMode == errModeNoSecondResponse) {
            // No response for all subsequent requests
            return;
        }

        if (_errMode == errModeNoSecondResponseAllowRetry) {
            // No response to this request, subsequent requests will succeed
            _errMode = errModeNone;
            return;
        }
    }

    ackResponse.hdr.opcode = MavlinkFTP::kRspAck;
    ackResponse.hdr.req_opcode = MavlinkFTP::kCmdListDirectory;
    ackResponse.hdr.session = 0;
    ackResponse.hdr.offset = request->hdr.offset;
    ackResponse.hdr.size = 0;

    // MockLink sends two directory entries per packet for a maximum of 3 packets, 6 total entries
    if (request->hdr.offset <= 5) {
        char *bufPtr = reinterpret_cast<char*>(&ackResponse.data[0]);
        QString dirEntry = QStringLiteral("Ffile%1.txt").arg(request->hdr.offset);
        auto cchDirEntry = dirEntry.length();
        (void) strncpy(bufPtr, dirEntry.toStdString().c_str(), cchDirEntry);
        ackResponse.hdr.size += dirEntry.length() + 1;
        bufPtr += cchDirEntry + 1;
        dirEntry = QStringLiteral("Ffile%1.txt").arg(request->hdr.offset + 1);
        cchDirEntry = dirEntry.length();
        (void) strncpy(bufPtr, dirEntry.toStdString().c_str(), cchDirEntry);
        ackResponse.hdr.size += dirEntry.length() + 1;
    } else {
        ackResponse.hdr.opcode = MavlinkFTP::kRspNak;
        ackResponse.data[0] = MavlinkFTP::kErrEOF;
        ackResponse.hdr.size = 1;
    }

    _sendResponse(senderSystemId, senderComponentId, &ackResponse, outgoingSeqNumber);
}

void MockVehicleFTP::_openCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    MavlinkFTP::Request response{};
    ensureNullTemination(request);
    const QString path = reinterpret_cast<char*>(request->data);

    // qDebug() << "Opening command " << path;

    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    const size_t cchPath = strnlen(reinterpret_cast<char*>(request->data), sizeof(request->data));
    Q_ASSERT(cchPath != sizeof(request->data));
    Q_UNUSED(cchPath); // Fix initialized-but-not-referenced warning on release builds

    _currentFile.close();

    QString tmpFilename;
    const QString sizePrefix = sizeFilenamePrefix;
    if (path.startsWith(sizePrefix)) {
        const QString sizeString = path.right(path.length() - sizePrefix.length());
        // tmpFilename = _createTestTempFile(sizeString.toInt());
    } else if (path == "/general.json") {
        tmpFilename = QStringLiteral(":MockVehicle/General.MetaData.json");
    } else if (path == "/general.json.xz") {
        tmpFilename = QStringLiteral(":MockVehicle/General.MetaData.json.xz");
    } else if (path == "/parameter.json") {
        tmpFilename = QStringLiteral(":MockVehicle/Parameter.MetaData.json");
    } else if (path == "/parameter.json.xz") {
        tmpFilename = QStringLiteral(":MockVehicle/Parameter.MetaData.json.xz");
    } else if (_BinParamFileEnabled && (path == "@PARAM/param.pck")) {
        tmpFilename = ":MockVehicle/ArduCopter.params.ftp.bin";
    }

    if (!tmpFilename.isEmpty()) {
        _currentFile.setFileName(tmpFilename);
        if (!_currentFile.open(QIODevice::ReadOnly)) {
            _sendNakErrno(senderSystemId, senderComponentId, _currentFile.error(), outgoingSeqNumber, MavlinkFTP::kCmdOpenFileRO);
            return;
        }
    } else {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailFileNotFound, outgoingSeqNumber, MavlinkFTP::kCmdOpenFileRO);
        return;
    }

    response.hdr.opcode = MavlinkFTP::kRspAck;
    response.hdr.req_opcode = MavlinkFTP::kCmdOpenFileRO;
    response.hdr.session = _sessionId;

    // Data contains file length
    response.hdr.size = sizeof(uint32_t);

    // Ardupilot sends constant wrong file size for parameter file due to dynamic on the fly generation
    // response.openFileLength = ((path == "@PARAM/param.pck") ? qPow(1024, 2) : _currentFile.size());
    
    // hoppz: ignore qgc way to get file size
    response.openFileLength = _currentFile.size();

    _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);
}

void MockVehicleFTP::_createCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    MavlinkFTP::Request response{};
    ensureNullTemination(request);

    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    const QString path = reinterpret_cast<char*>(request->data);
    /// temp/ftp/drone_1/collmot/show.skyb
    const QString tempPath =  ZSMApplication::applicationDir() + ZSM_FTP_DIR + QString("/drone_%1").arg(_vehicle->vehicleId()) + path;
    const QString tempDir = QFileInfo(tempPath).absolutePath();
    const size_t cchPath = strnlen(reinterpret_cast<char*>(request->data), sizeof(request->data));
    Q_ASSERT(cchPath != sizeof(request->data));
    Q_UNUSED(cchPath); // Fix initialized-but-not-referenced warning on release builds

    // A new show upload replaces the previous file. Do not report a loaded
    // show again until the client has verified the new file by CRC.
    if (path == QStringLiteral("/collmot/show.skyb")) {
        _showFileUploaded = false;
    }

    // TODO 判断 _currentFile 是否已经打开

    // 判断 Dir 是否存在
    QDir outputDir(tempDir);

    //TODO QFile Helper
    if ( !outputDir.exists() ) {
        if ( !outputDir.mkpath(tempDir) ) {
            zWarning(MockVehicleFTPLog) << "Failed to create output directory:" << tempDir;
            _sendNakErrno(senderSystemId, senderComponentId, QFileDevice::FatalError, outgoingSeqNumber, MavlinkFTP::kCmdCreateFile);
            return ;
        }
    } else {
        // 如果存在则清空
        QFileInfoList fileList = outputDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo& fileInfo : fileList) {
            if (fileInfo.isDir()) {
                QDir(fileInfo.absoluteFilePath()).removeRecursively();
            }
            else {
                QFile::remove(fileInfo.absoluteFilePath());
            }
        }
    }
    _currentFile.close();
    _currentFile.setFileName(tempPath);

    // 创建文件
    if (!_currentFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        zWarning(MockVehicleFTPLog) << "Failed to create file:" << tempPath << _currentFile.errorString();
        _sendNakErrno(senderSystemId, senderComponentId, MavlinkFTP::kErrFail, outgoingSeqNumber, MavlinkFTP::kCmdCreateFile);
        return ;
    }
    zDebug(MockVehicleFTPLog) << "Created file:" << tempPath;
    response.hdr.opcode = MavlinkFTP::kRspAck;
    response.hdr.req_opcode = MavlinkFTP::kCmdCreateFile;
    response.hdr.session = _sessionId;
    response.hdr.size = 0;

    _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);
}

void MockVehicleFTP::_writeCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    MavlinkFTP::Request	response{};
    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    if (request->hdr.session != _sessionId) {
        zWarning(MockVehicleFTPLog) << "session not match.";
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrInvalidSession, outgoingSeqNumber, MavlinkFTP::kCmdWriteFile);
        return;
    }

    const uint32_t writeOffset = request->hdr.offset;  // offset into file for writing
    const uint8_t* payload = request->data;            // data to write
    const size_t payloadLen = request->hdr.size;

    if (!_currentFile.isOpen()) {
        zWarning(MockVehicleFTPLog) << "File is not open for writing.";
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailErrno, outgoingSeqNumber, MavlinkFTP::kCmdWriteFile);
        return;
    }

    if (!_currentFile.seek(writeOffset)) {
        zWarning(MockVehicleFTPLog) << "Seek failed at offset:" << writeOffset;
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailErrno, outgoingSeqNumber, MavlinkFTP::kCmdWriteFile);
        return;
    }

    qint64 bytesWritten = _currentFile.write(reinterpret_cast<const char*>(payload), payloadLen);
    zDebug(MockVehicleFTPLog)   << "offset:" << writeOffset
                                << "payloadLen:" << payloadLen
		                        << "bytesWritten:" << bytesWritten;
    if (bytesWritten != static_cast<qint64>(payloadLen)) {
        zWarning(MockVehicleFTPLog) << "Write failed. Expected:" << payloadLen << "Wrote:" << bytesWritten;
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailErrno, outgoingSeqNumber, MavlinkFTP::kCmdWriteFile);
        return;
    }

    if (!_currentFile.flush()) {
        zWarning(MockVehicleFTPLog) << "Flush failed.";
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailErrno, outgoingSeqNumber, MavlinkFTP::kCmdWriteFile);
        return;
    }

    // 成功写入，发送 ACK
    response.hdr.opcode = MavlinkFTP::kRspAck;
    response.hdr.session = _sessionId;
    response.hdr.size = 0;
    response.hdr.req_opcode = MavlinkFTP::kCmdWriteFile;
    response.hdr.offset = request->hdr.offset;

    _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);

}

void MockVehicleFTP::_calcFileCrcCommand(uint16_t senderSystemId, uint8_t senderComponentId,
    MavlinkFTP::Request *request, uint16_t seqNumber)
{
    ensureNullTemination(request);
    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);
    const QString remotePath = QString::fromUtf8(reinterpret_cast<const char*>(request->data));
    const QString localPath = ZSMApplication::applicationDir()
        + ZSM_FTP_DIR
        + QString("/drone_%1").arg(_vehicle->vehicleId())
        + remotePath;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFailFileNotFound,
            outgoingSeqNumber, MavlinkFTP::kCmdCalcFileCRC32);
        return;
    }

    quint32 crc = 0;
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(64 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) {
            _sendNakErrno(senderSystemId, senderComponentId, file.error(),
                outgoingSeqNumber, MavlinkFTP::kCmdCalcFileCRC32);
            return;
        }
        crc = updateCrc32(crc, bytes);
    }

    MavlinkFTP::Request response{};
    response.hdr.opcode = MavlinkFTP::kRspAck;
    response.hdr.req_opcode = MavlinkFTP::kCmdCalcFileCRC32;
    response.hdr.session = 0;
    response.hdr.size = sizeof(crc);
    qToLittleEndian<quint32>(crc, response.data);
    if (remotePath == QStringLiteral("/collmot/show.skyb")) {
        _showFileUploaded = true;
    }
    _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);
}

void MockVehicleFTP::_readCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
                                  uint16_t seqNumber)
{
    MavlinkFTP::Request	response{};
    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    if (request->hdr.session != _sessionId) {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrInvalidSession, outgoingSeqNumber, MavlinkFTP::kCmdReadFile);
        return;
    }

    const uint32_t readOffset = request->hdr.offset;  // offset into file for reading
    if (readOffset != 0) {
        // If we get here it means the client is requesting additional data past the first request
        if (_errMode == errModeNakSecondResponse) {
            // Nak error all subsequent requests
            _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFail, outgoingSeqNumber, MavlinkFTP::kCmdReadFile);
            return;
        }

        if (_errMode == errModeNoSecondResponse) {
            // No rsponse for all subsequent requests
            return;
        }
    }

    if (readOffset >= _currentFile.size()) {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrEOF, outgoingSeqNumber, MavlinkFTP::kCmdReadFile);
        return;
    }

    const uint8_t cBytesToRead = static_cast<uint8_t>(qMin(static_cast<qint64>(sizeof(response.data)), _currentFile.size() - readOffset));
    (void) _currentFile.seek(readOffset);
    const QByteArray bytes = _currentFile.read(cBytesToRead);
    (void) memcpy(response.data, bytes.constData(), cBytesToRead);

    // We should always have written something, otherwise there is something wrong with the code above
    Q_ASSERT(cBytesToRead);

    response.hdr.session = _sessionId;
    response.hdr.size = cBytesToRead;
    response.hdr.offset = request->hdr.offset;
    response.hdr.opcode = MavlinkFTP::kRspAck;
    response.hdr.req_opcode = MavlinkFTP::kCmdReadFile;

    _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);
}

void MockVehicleFTP::_burstReadCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    MavlinkFTP::Request response{};
    uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    if (request->hdr.session != _sessionId) {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrFail, outgoingSeqNumber, MavlinkFTP::kCmdBurstReadFile);
        return;
    }

    constexpr int burstMax = 10;
    int burstCount = 1;
    uint32_t burstOffset = request->hdr.offset;

    while ((burstOffset < _currentFile.size()) && (burstCount++ < burstMax)) {
        _currentFile.seek(burstOffset);

        const uint8_t cBytes = static_cast<uint8_t>(qMin(static_cast<qint64>(sizeof(response.data)), _currentFile.size() - burstOffset));
        const QByteArray bytes = _currentFile.read(cBytes);
        Q_ASSERT(cBytes); // We should always have written something, otherwise there is something wrong with the code above

        (void) memcpy(response.data, bytes.constData(), cBytes);

        response.hdr.session = _sessionId;
        response.hdr.size = cBytes;
        response.hdr.offset = burstOffset;
        response.hdr.opcode = MavlinkFTP::kRspAck;
        response.hdr.req_opcode = MavlinkFTP::kCmdBurstReadFile;
        response.hdr.burstComplete = (burstCount == burstMax) ? 1 : 0;

        _sendResponse(senderSystemId, senderComponentId, &response, outgoingSeqNumber);

        outgoingSeqNumber = _nextSeqNumber(outgoingSeqNumber);
        burstOffset += cBytes;
    }

    if (burstOffset >= _currentFile.size()) {
        // Burst is fully complete
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrEOF, outgoingSeqNumber, MavlinkFTP::kCmdBurstReadFile);
    }
}

void MockVehicleFTP::_terminateCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request,
    uint16_t seqNumber)
{
    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    _currentFile.close();
    zDebug(MockVehicleFTPLog) << "get terminate";

    if (request->hdr.session != _sessionId) {
        _sendNak(senderSystemId, senderComponentId, MavlinkFTP::kErrInvalidSession, outgoingSeqNumber, MavlinkFTP::kCmdTerminateSession);
        return;
    }


    _sendAck(senderSystemId, senderComponentId, outgoingSeqNumber, MavlinkFTP::kCmdTerminateSession);

    // emit terminateCommandReceived();
}

void MockVehicleFTP::_resetCommand(uint16_t senderSystemId, uint8_t senderComponentId, uint16_t seqNumber)
{
    const uint16_t outgoingSeqNumber = _nextSeqNumber(seqNumber);

    _currentFile.close();
    _sendAck(senderSystemId, senderComponentId, outgoingSeqNumber, MavlinkFTP::kCmdResetSessions);

    // emit resetCommandReceived();
}

uint16_t MockVehicleFTP::_nextSeqNumber(uint16_t seqNumber) const
{
    uint16_t outgoingSeqNumber = seqNumber + 1;

    if (_errMode == errModeBadSequence) {
        outgoingSeqNumber++;
    }

    return outgoingSeqNumber;
}

// QString MockVehicleFTP::_createTestTempFile(int size)
// {
//     QGCTemporaryFile tmpFile("MockLinkFTPTestCase");
//
//     if (tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
//         for (int i = 0; i < size; i++) {
//             (void) tmpFile.write(QByteArray(1, i % 255));
//         }
//         tmpFile.close();
//     }
//
//     return tmpFile.fileName();
// }

void MockVehicleFTP::ensureNullTemination(MavlinkFTP::Request *request)
{
    if (request->hdr.size < sizeof(request->data)) {
        request->data[request->hdr.size] = '\0';
    } else {
        request->data[sizeof(request->data) - 1] = '\0';
    }
}
