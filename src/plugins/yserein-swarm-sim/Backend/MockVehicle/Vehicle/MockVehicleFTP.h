//
// Created by Hoppz on 25-7-2.
//
#pragma once

#include <QObject>
#include <QtCore/QFile>

#include "MAVLinkFTP.h"
#include "MavlinkLib.h"

class MockVehicle;

class MockVehicleFTP : public QObject {
    Q_OBJECT
public:
    MockVehicleFTP(uint16_t systemIdServer, uint16_t componentIdServer, MockVehicle* vehicle);
    ~MockVehicleFTP() override;

    /// By calling setErrorMode with one of these modes you can cause the server to simulate an error.
    enum ErrorMode_t {
        errModeNone,                        ///< No error, respond correctly
        errModeNoResponse,                  ///< No response to any request, client should eventually time out with no Ack
        errModeNakResponse,                 ///< Nak all requests
        errModeNoSecondResponse,            ///< No response to subsequent request to initial command
        errModeNoSecondResponseAllowRetry,  ///< No response to subsequent request to initial command, error will be cleared after this so retry will succeed
        errModeNakSecondResponse,           ///< Nak subsequent request to initial command
        errModeBadSequence                  ///< Return response with bad sequence number
    };

    /// Array of failure modes you can cycle through for testing. By looping through this array you can avoid
    /// hardcoding the specific error modes in your unit test. This way when new error modes are added your unit test
    /// code may not need to be modified.
    static constexpr const ErrorMode_t rgFailureModes[] = {
        errModeNoResponse,
        errModeNakResponse,
        errModeNoSecondResponse,
        errModeNakSecondResponse,
        errModeBadSequence,
    };

    void mavlinkMessageReceived(const mavlink_message_t& message);
    bool hasUploadedShowFile() const { return _showFileUploaded; }

    static constexpr const char *sizeFilenamePrefix = "mocklink-size-";

private:
    /// Sends an Ack
    void _sendAck(uint16_t targetSystemId, uint8_t targetComponentId, uint16_t seqNumber, MavlinkFTP::OpCode_t reqOpcode);
    void _sendNak(uint16_t targetSystemId, uint8_t targetComponentId, MavlinkFTP::ErrorCode_t error, uint16_t seqNumber, MavlinkFTP::OpCode_t reqOpcode);
    void _sendNakErrno(uint16_t targetSystemId, uint8_t targetComponentId, uint8_t nakErrno, uint16_t seqNumber, MavlinkFTP::OpCode_t reqOpcode);
    /// Emits a Request through the messageReceived signal.
    void _sendResponse(uint16_t targetSystemId, uint8_t targetComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    /// Handles List command requests. Only supports root folder paths.
    /// File list returned is set using the setFileList method.
    void _listCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _openCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _createCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _writeCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _calcFileCrcCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _readCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _burstReadCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _terminateCommand(uint16_t senderSystemId, uint8_t senderComponentId, MavlinkFTP::Request *request, uint16_t seqNumber);
    void _resetCommand(uint16_t senderSystemId, uint8_t senderComponentId, uint16_t seqNumber);
    /// Generates the next sequence number given an incoming sequence number. Handles generating
    /// bad sequence numbers when errModeBadSequence is set.
    uint16_t _nextSeqNumber(uint16_t seqNumber) const;
    // static QString _createTestTempFile(int size);

    /// if request is a string, this ensures it's null-terminated
    static void ensureNullTemination(MavlinkFTP::Request *request);

private:

    uint16_t _systemIdServer;               ///< vehicle -> systemId
    uint16_t _componentIdServer;            ///< Vehicle -> componentId
    MockVehicle* _vehicle;

    bool _randomDropsEnabled = false;
    bool _BinParamFileEnabled = true;
    bool _lastReplyValid = false;
    uint16_t _lastReplySequence = 0;
    mavlink_message_t _lastReply{};
    ErrorMode_t _errMode = errModeNone;         ///< Currently set error mode, as specified by setErrorMode
    QFile _currentFile;
    bool _showFileUploaded = false;

    static constexpr uint8_t _sessionId = 1;    ///< We only support a single fixed session
};
