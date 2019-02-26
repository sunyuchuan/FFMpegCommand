#ifndef XM_FFCMD_MSG_DEF_H
#define XM_FFCMD_MSG_DEF_H
#include "ff_ffmsg_queue.h"

#define FFCMD_STATE_UNINIT  0
#define FFCMD_STATE_INITIALIZED  1
#define FFCMD_STATE_ASYNC_PREPARING  2
#define FFCMD_STATE_PREPARED  3
#define FFCMD_STATE_STARTED  4
#define FFCMD_STATE_STOPPED  5
#define FFCMD_STATE_COMPLETED  6
#define FFCMD_STATE_ERROR  7

#define FFCMD_INFO_PREPARED 100
#define FFCMD_INFO_STARTED  200
#define FFCMD_INFO_PROGRESS  300
#define FFCMD_INFO_STOPPED  400
#define FFCMD_INFO_COMPLETED  500
#define FFCMD_MSG_STATE_CHANGED  600
#define FFCMD_MSG_ERROR 700

#define FFCMD_REQ_START  800
#define FFCMD_REQ_STOP  900

#define XM_LOG_UNKNOWN     0
#define XM_LOG_DEFAULT     1
#define XM_LOG_VERBOSE     2
#define XM_LOG_DEBUG       3
#define XM_LOG_INFO        4
#define XM_LOG_WARN        5
#define XM_LOG_ERROR       6
#define XM_LOG_FATAL       7
#define XM_LOG_SILENT      8

enum cmd_event_type {
    FFCMD_NOP = 0,
    FFCMD_ERROR = 1,
    FFCMD_INFO = 2,
};

inline static void ffcmd_notify_msg1(MessageQueue *msg_queue, int what) {
    if(msg_queue)
        msg_queue_put_simple3(msg_queue, what, 0, 0);
}

inline static void ffcmd_notify_msg2(MessageQueue *msg_queue, int what, int arg1) {
    if(msg_queue)
        msg_queue_put_simple3(msg_queue, what, arg1, 0);
}

inline static void ffcmd_notify_msg3(MessageQueue *msg_queue, int what, int arg1, int arg2) {
    if(msg_queue)
        msg_queue_put_simple3(msg_queue, what, arg1, arg2);
}

#endif
