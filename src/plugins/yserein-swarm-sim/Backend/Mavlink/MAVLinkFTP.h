/**
 * @file MAVLinkFTP.h
 * @author Hoppz (hoppz520@163.com)
 * @brief
 * @version 0.1
 * @date 2025-01-13
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include "MAVLinkLib.h"
#include <QtCore/QString>

class MavlinkFTP
{

public:
    /// FILE_TRANSFER_PROTOCOL (110) 的头部
    MAVPACKED(
        typedef struct RequestHeader {
            uint16_t seqNumber;    ///< 消息的序列号
            uint8_t session;       ///< 读写操作的会话 id
            uint8_t opcode;        ///< FTP 的操作 id
            uint8_t size;          ///< data 的 size
            uint8_t req_opcode;    ///< 在 ACK 或者 NAK 中返回的操作 id
            uint8_t burstComplete; ///< Burst 是否完成
            uint8_t padding;       ///< 32位对齐填充
            uint32_t offset;       ///< List 和 Read 操作的偏移量
        })
    RequestHeader;

    /// FILE_TRANSFER_PROTOCOL (110) 的 data
    MAVPACKED(
        typedef struct Request {
            RequestHeader hdr;

            union
            {
                uint8_t data[sizeof(((mavlink_file_transfer_protocol_t *)0)->payload) - sizeof(RequestHeader)];

                uint32_t openFileLength;

                uint32_t writeFileLength;
            };
        })
    Request;

    /// OpCodes/Command
    /// The opcodes that may be sent by the GCS (client) to the drone (server) are listed below.
    typedef enum
    {
        kCmdNone = 0,         ///< ignored, always acked
        kCmdTerminateSession, ///< 终止指定 Read session
        kCmdResetSessions,    ///< 终止所有 Read session
        kCmdListDirectory,    ///< List files in <path> from <offset>
        kCmdOpenFileRO,       ///< Opens file at <path> for reading, returns <session>
        kCmdReadFile,         ///< Reads <size> bytes from <offset> in <session>
        kCmdCreateFile,       ///< Creates file at <path> for writing, returns <session>
        kCmdWriteFile,        ///< Writes <size> bytes to <offset> in <session>
        kCmdRemoveFile,       ///< Remove file at <path>
        kCmdCreateDirectory,  ///< Creates directory at <path>
        kCmdRemoveDirectory,  ///< Removes Directory at <path>, must be empty
        kCmdOpenFileWO,       ///< Opens file at <path> for writing, returns <session>
        kCmdTruncateFile,     ///< Truncate file at <path> to <offset> length
        kCmdRename,           ///< Rename <path1> to <path2>
        kCmdCalcFileCRC32,    ///< Calculate CRC32 for file at <path>
        kCmdBurstReadFile,    ///< Burst download session file

        kRspAck = 128, ///< Ack response
        kRspNak,       ///< Nak response
    } OpCode_t;

    /// @brief Error codes returned in Nak response PayloadHeader.data[0].
    typedef enum
    {
        kErrNone = 0,
        kErrFail,                ///< Unknown failure
        kErrFailErrno,           ///< errno sent back in PayloadHeader.data[1]
        kErrInvalidDataSize,     ///< PayloadHeader.size is invalid
        kErrInvalidSession,      ///< Session is not currently open
        kErrNoSessionsAvailable, ///< All available Sessions in use
        kErrEOF,                 ///< Offset past end of file for List and Read commands
        kErrUnknownCommand,      ///< Unknown command opcode
        kErrFailFileExists,      ///< File exists already
        kErrFailFileProtected,   ///< File is write protected
        kErrFailFileNotFound
    } ErrorCode_t;

    static QString opCodeToString(OpCode_t opCode);
    static QString errorCodeToString(ErrorCode_t errorCode);
};
