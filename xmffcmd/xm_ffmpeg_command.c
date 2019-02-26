#include "xm_ffmpeg_command.h"
#include "xm_ffcmd_msg_def.h"
#include "ffmpeg.h"
#include "ijksdl/android/ijksdl_android_jni.h"
#include "ijksdl/ijksdl_log.h"

#define FFCMD_FAILED -1
#define FFCMD_OUT_OF_MEMORY -2
#define FFCMD_INVALID_STATE -3
#define FFCMD_NULL_IS_PTR -4

#define MP_RET_IF_FAILED(ret) \
    do { \
        int retval = ret; \
        if (retval != 0) return (retval); \
    } while(0)

#define MPST_RET_IF_EQ_INT(real, expected, errcode) \
    do { \
        if ((real) == (expected)) return (errcode); \
    } while(0)

#define MPST_RET_IF_EQ(real, expected) \
    MPST_RET_IF_EQ_INT(real, expected, FFCMD_INVALID_STATE)

static int ffcmd_thread_stop(XMFFmpegCmd *cmd);

static void ffcmd_free_l(XMFFmpegCmd *cmd)
{
    ALOGD("ffcmd_free_l");
    if (!cmd)
        return;

    if (cmd->cmd.argv != NULL) {
        for (int i = 0; i < cmd->cmd.argc; i++) {
            if (cmd->cmd.argv[i] != NULL)
                av_free(cmd->cmd.argv[i]);
        }
        av_free(cmd->cmd.argv);
    }

    pthread_mutex_destroy(&cmd->mutex);
    msg_queue_destroy(&cmd->msg_queue);
}

static int ffcmd_freep_l(XMFFmpegCmd **cmd)
{
    if(!cmd || !*cmd)
        return -1;
    int cmd_state = (*cmd)->cmd_state;

    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_UNINIT);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_INITIALIZED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_PREPARED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STARTED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_COMPLETED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STOPPED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ERROR);

    ffcmd_free_l(*cmd);
    free(*cmd);
    *cmd = NULL;
    return 0;
}

static void ffcmd_change_state_l(XMFFmpegCmd *cmd, int new_state)
{
    cmd->cmd_state = new_state;
    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_MSG_STATE_CHANGED);
}

static int ffcmd_msg_loop(void *arg)
{
    XMFFmpegCmd *cmd = (XMFFmpegCmd *)arg;
    int ret = cmd->msg_loop(arg);
    return ret;
}

void *ffcmd_get_weak_thiz(XMFFmpegCmd *cmd)
{
    if (!cmd)
        return NULL;

    return cmd->weak_thiz;
}

void *ffcmd_set_weak_thiz(XMFFmpegCmd *cmd, void *weak_thiz)
{
    if (!cmd)
        return NULL;

    void *prev_weak_thiz = cmd->weak_thiz;

    cmd->weak_thiz = weak_thiz;

    return prev_weak_thiz;
}

void ffcmd_inc_ref(XMFFmpegCmd *cmd)
{
    assert(cmd);
    __sync_fetch_and_add(&cmd->ref_count, 1);
}

void ffcmd_dec_ref(XMFFmpegCmd *cmd)
{
    if (!cmd)
        return;

    int ref_count = __sync_sub_and_fetch(&cmd->ref_count, 1);
    if (ref_count == 0) {
        ALOGD("ffcmd_dec_ref(): ref=0\n");
        ffcmd_thread_stop(cmd);
        ffcmd_freep_l(&cmd);
    }
}

void ffcmd_dec_ref_p(XMFFmpegCmd **cmd)
{
    if (!cmd || !*cmd)
        return;

    ffcmd_dec_ref(*cmd);
    *cmd = NULL;
}

static int ffcmd_chkst_prepareAsync_l(int cmd_state)
{
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_UNINIT);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_INITIALIZED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_PREPARED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STARTED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_COMPLETED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STOPPED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ERROR);

    return 0;
}

static int ffcmd_chkst_restart_l(int cmd_state)
{
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_UNINIT);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_INITIALIZED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ASYNC_PREPARING);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_PREPARED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STARTED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_COMPLETED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STOPPED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ERROR);

    return 0;
}

static int ffcmd_chkst_start_l(int cmd_state)
{
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_UNINIT);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_INITIALIZED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ASYNC_PREPARING);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_PREPARED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STARTED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_COMPLETED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STOPPED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ERROR);

    return 0;
}

static int ffcmd_chkst_stop_l(int cmd_state)
{
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_UNINIT);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_INITIALIZED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ASYNC_PREPARING);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_PREPARED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STARTED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_COMPLETED);
    MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_STOPPED);
    //MPST_RET_IF_EQ(cmd_state, FFCMD_STATE_ERROR);

    return 0;
}

static int ffcmd_ffmpeg(void *arg)
{
    if (!arg)
        return -1;

    JNIEnv *env = NULL;

    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("ffcmd_ffmpeg: SetupThreadEnv failed\n");
        goto fail;
    }

    XMFFmpegCmd *cmd = (XMFFmpegCmd *)arg;

    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_INFO_STARTED);
    cmd->mRunning = true;

    int ret = ffmpeg_main(cmd->cmd.argc, cmd->cmd.argv, cmd);
    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_INFO_COMPLETED);
    cmd->mRunning = false;
    return ret;

fail:
    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_MSG_ERROR);
    cmd->mRunning = false;
    return -1;
}

static int ffcmd_wait(XMFFmpegCmd *cmd)
{
    if (!cmd || !cmd->mRunning)
        return 0;

    if (cmd->cmd.ffcmd_thread) {
        SDL_WaitThread(cmd->cmd.ffcmd_thread, NULL);
        cmd->cmd.ffcmd_thread = NULL;
    }

    cmd->mRunning = false;
    return 0;
}

static void ffcmd_abort(XMFFmpegCmd *cmd)
{
    if (!cmd)
        return;

    pthread_mutex_lock(&cmd->mutex);
    cmd->abort = true;
    pthread_mutex_unlock(&cmd->mutex);
}

static int ffcmd_thread_stop(XMFFmpegCmd *cmd)
{
    int ret = -1;
    if (!cmd)
        return ret;

    cmd->abort = true;
    if ((ret = ffcmd_wait(cmd)) != 0)
        ALOGD("Couldn't cancel ffcmd_ffmpeg %d", ret);

    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_INFO_STOPPED);
    return ret;
}

static int ffcmd_thread_start(XMFFmpegCmd *cmd)
{
    if(!cmd)
        return -1;

    cmd->abort = false;
    cmd->cmd.ffcmd_thread = SDL_CreateThreadEx(&cmd->cmd._ffcmd_thread, ffcmd_ffmpeg, cmd, "ffcmd_ffmpeg");
    if (!cmd->cmd.ffcmd_thread) {
        ALOGE("SDL_CreateThread() failed : %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int ffcmd_get_msg(XMFFmpegCmd *cmd, AVMessage *msg, int block)
{
    assert(cmd);
    while (1) {
        int continue_wait_next_msg = 0;
        int retval = msg_queue_get(&cmd->msg_queue, msg, block);
        if (retval <= 0)
            return retval;

        switch (msg->what) {
        case FFP_MSG_FLUSH:
            ALOGD("ffcmd_get_msg: FFP_MSG_FLUSH\n");
            pthread_mutex_lock(&cmd->mutex);
            if (cmd->cmd_state == FFCMD_STATE_ASYNC_PREPARING) {
                ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_INFO_PREPARED);
            }
            pthread_mutex_unlock(&cmd->mutex);
            break;

	    case FFCMD_INFO_PREPARED:
            ALOGD("ffcmd_get_msg: FFCMD_INFO_PREPARED\n");
            pthread_mutex_lock(&cmd->mutex);
            if (cmd->cmd_state == FFCMD_STATE_ASYNC_PREPARING) {
                ffcmd_change_state_l(cmd, FFCMD_STATE_PREPARED);
            } else {
                ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_MSG_ERROR);
                ALOGE("FFCMD_INFO_PREPARED: expecting cmd_state == FFCMD_STATE_ASYNC_PREPARING\n");
            }
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_REQ_START:
            ALOGD("ffcmd_get_msg: FFCMD_REQ_START\n");
            continue_wait_next_msg = 1;
            pthread_mutex_lock(&cmd->mutex);
            if (0 == ffcmd_chkst_start_l(cmd->cmd_state)) {
                ffcmd_thread_start(cmd);
            } else {
                ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_MSG_ERROR);
                ALOGE("FFCMD_REQ_START: expecting cmd_state == prepared\n");
            }
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_INFO_STARTED:
            ALOGD("ffcmd_get_msg: FFCMD_INFO_STARTED\n");
            pthread_mutex_lock(&cmd->mutex);
            ffcmd_change_state_l(cmd, FFCMD_STATE_STARTED);
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_REQ_STOP:
            ALOGD("ffcmd_get_msg: FFCMD_REQ_STOP\n");
            continue_wait_next_msg = 1;
            pthread_mutex_lock(&cmd->mutex);
            if (0 == ffcmd_chkst_stop_l(cmd->cmd_state)) {
                 ffcmd_thread_stop(cmd);
            } else {
                 ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_INFO_STOPPED);
            }
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_INFO_STOPPED:
            ALOGD("ffcmd_get_msg: FFCMD_INFO_STOPPED\n");
            pthread_mutex_lock(&cmd->mutex);
            ffcmd_change_state_l(cmd, FFCMD_STATE_STOPPED);
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_INFO_COMPLETED:
            ALOGD("ffcmd_get_msg: FFCMD_INFO_COMPLETED\n");
            pthread_mutex_lock(&cmd->mutex);
            ffcmd_change_state_l(cmd, FFCMD_STATE_COMPLETED);
            pthread_mutex_unlock(&cmd->mutex);
            break;

        case FFCMD_INFO_PROGRESS:
            ALOGD("ffcmd_get_msg: FFCMD_INFO_PROGRESS %d\n", msg->arg1);
            break;

        case FFCMD_MSG_ERROR:
            ALOGD("ffcmd_get_msg: FFCMD_MSG_ERROR\n");
            pthread_mutex_lock(&cmd->mutex);
            ffcmd_thread_stop(cmd);
            ffcmd_change_state_l(cmd, FFCMD_STATE_ERROR);
            pthread_mutex_unlock(&cmd->mutex);
            break;
        }

        if (continue_wait_next_msg)
            continue;

        return retval;
    }

    return -1;
}

void xm_ffmpeg_cmd_msg_thread_exit(XMFFmpegCmd *cmd)
{
    if (!cmd)
        return;

    msg_queue_abort(&cmd->msg_queue);
    if (cmd->msg_thread) {
        SDL_WaitThread(cmd->msg_thread, NULL);
        cmd->msg_thread = NULL;
    }
}

void xm_ffmpeg_cmd_stop(XMFFmpegCmd *cmd)
{
    ALOGD("xm_ffmpeg_cmd_stop()\n");
    if (!cmd)
        return;

    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_REQ_STOP);
}

void xm_ffmpeg_cmd_start(XMFFmpegCmd *cmd, int argc, char **argv)
{
    ALOGD("xm_ffmpeg_cmd_start()\n");
    if (!cmd || !argv || !*argv)
        return;

    if (cmd->cmd.argv != NULL) {
        for (int i = 0; i < cmd->cmd.argc; i++) {
            if (cmd->cmd.argv[i] != NULL)
                av_free(cmd->cmd.argv[i]);
        }
        av_free(cmd->cmd.argv);
    }

    cmd->cmd.argc = argc;
    cmd->cmd.argv = (char**)av_mallocz(sizeof(char*) * argc);
    for (int i = 0; i < argc; i++) {
        cmd->cmd.argv[i] = av_strdup(argv[i]);
    }
    ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_REQ_START);
}

int xm_ffmpeg_cmd_prepareAsync(XMFFmpegCmd *cmd)
{
    ALOGD("xm_ffmpeg_cmd_prepareAsync()\n");
    if (!cmd)
        return -1;

    if (ffcmd_chkst_restart_l(cmd->cmd_state) == 0) {
        ffcmd_change_state_l(cmd, FFCMD_STATE_ASYNC_PREPARING);
        msg_queue_start(&cmd->msg_queue);
        ALOGD("ffcmd restart\n");
        return 0;
    }

    if (ffcmd_chkst_prepareAsync_l(cmd->cmd_state) != 0) {
        if (cmd && !cmd->msg_queue.abort_request) {
            ffcmd_notify_msg1(&cmd->msg_queue, FFCMD_MSG_ERROR);
        }
        ALOGE("ffcmd prepareAsync failed, expecting cmd_state == FFCMD_STATE_INITIALIZED\n");
        return -1;
    }

    ffcmd_change_state_l(cmd, FFCMD_STATE_ASYNC_PREPARING);

    msg_queue_start(&cmd->msg_queue);

    // released in msg_loop
    ffcmd_inc_ref(cmd);
    // msg_thread is detached inside msg_loop
    cmd->msg_thread = SDL_CreateThreadEx(&cmd->_msg_thread, ffcmd_msg_loop, cmd, "ffcmd_msg_loop");
    if (!cmd->msg_thread) {
        ALOGE("SDL_CreateThread() failed : %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

XMFFmpegCmd *xm_ffmpeg_cmd_create(int(*msg_loop)(void*))
{
    XMFFmpegCmd *cmd = (XMFFmpegCmd *)calloc(1, sizeof(XMFFmpegCmd));
    if (!cmd)
        return NULL;

    cmd->msg_loop = msg_loop;
    pthread_mutex_init(&cmd->mutex, NULL);
    msg_queue_init(&cmd->msg_queue);
    ffcmd_inc_ref(cmd);
    ffcmd_change_state_l(cmd, FFCMD_STATE_INITIALIZED);

    return cmd;
}

inline static int log_level_xm_to_av(int xm_level)
{
    int av_level = XM_LOG_VERBOSE;
    if      (xm_level >= XM_LOG_SILENT)   av_level = AV_LOG_QUIET;
    else if (xm_level >= XM_LOG_FATAL)    av_level = AV_LOG_FATAL;
    else if (xm_level >= XM_LOG_ERROR)    av_level = AV_LOG_ERROR;
    else if (xm_level >= XM_LOG_WARN)     av_level = AV_LOG_WARNING;
    else if (xm_level >= XM_LOG_INFO)     av_level = AV_LOG_INFO;
    else if (xm_level >= XM_LOG_DEBUG)    av_level = AV_LOG_DEBUG;
    else if (xm_level >= XM_LOG_VERBOSE)  av_level = AV_LOG_TRACE;
    else if (xm_level >= XM_LOG_DEFAULT)  av_level = AV_LOG_TRACE;
    else if (xm_level >= XM_LOG_UNKNOWN)  av_level = AV_LOG_TRACE;
    else                                    av_level = AV_LOG_TRACE;
    return av_level;
}

void xm_ffmpeg_set_log_level(int log_level)
{
    int av_level = log_level_xm_to_av(log_level);
    av_log_set_level(av_level);
}

void xm_ffmpeg_init()
{
    ALOGD("xm_ffmpeg_init\n");

    //av_log_set_callback(NULL);
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_register_all();
}

