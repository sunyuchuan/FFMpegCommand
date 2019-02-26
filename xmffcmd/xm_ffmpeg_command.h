#ifndef XM_FFMPEG_COMMAND_H
#define XM_FFMPEG_COMMAND_H
#include "ijksdl/ijksdl_thread.h"
#include "ijksdl/ijksdl_log.h"
#include "xm_ffcmd_msg_def.h"

typedef struct FFCmd {
    char **argv;
    int argc;
    SDL_Thread *ffcmd_thread;
    SDL_Thread _ffcmd_thread;
} FFCmd;

typedef struct XMFFmpegCmd
{
    volatile int ref_count;
    pthread_mutex_t mutex;
    FFCmd cmd;

    int (*msg_loop)(void*);
    SDL_Thread *msg_thread;
    SDL_Thread _msg_thread;

    bool mRunning;
    volatile bool abort;
    int cmd_state;
    void *weak_thiz;
    MessageQueue msg_queue;
} XMFFmpegCmd;

void *ffcmd_get_weak_thiz(XMFFmpegCmd *cmd);
void *ffcmd_set_weak_thiz(XMFFmpegCmd *cmd, void *weak_thiz);
void ffcmd_inc_ref(XMFFmpegCmd *cmd);
void ffcmd_dec_ref(XMFFmpegCmd *cmd);
void ffcmd_dec_ref_p(XMFFmpegCmd **cmd);
int ffcmd_get_msg(XMFFmpegCmd *cmd, AVMessage *msg, int block);

void xm_ffmpeg_cmd_msg_thread_exit(XMFFmpegCmd *cmd);
void xm_ffmpeg_cmd_stop(XMFFmpegCmd *cmd);
void xm_ffmpeg_cmd_start(XMFFmpegCmd *cmd, int argc, char **argv);
int xm_ffmpeg_cmd_prepareAsync(XMFFmpegCmd *cmd);
XMFFmpegCmd *xm_ffmpeg_cmd_create(int(*msg_loop)(void*));
void xm_ffmpeg_set_log_level(int log_level);
void xm_ffmpeg_init();
#endif
