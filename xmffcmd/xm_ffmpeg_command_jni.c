//
// Created by sunyc on 18-10-26.
//
#include "ijksdl/ijksdl_log.h"
#include "ijksdl/android/ijksdl_android_jni.h"
#include "ijksdl/ijksdl_misc.h"

#include "xm_ffmpeg_command.h"

#define TAG "xm_ffmpeg_command_jni"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define JNI_CLASS_XM_FFMPEG_COMMAND "com/xmly/media/co_production/FFmpegCommand"

typedef struct xm_ffmpeg_command_fields_t {
    pthread_mutex_t mutex;
    jclass clazz;
    jfieldID field_mNativeFFmpegCommand;
    jmethodID method_postEventFromNative;
} xm_ffmpeg_command_fields_t;

static xm_ffmpeg_command_fields_t g_clazz;
static JavaVM* g_jvm;

jlong jni_nativeXMFFmpegCommand_get(JNIEnv *env, jobject thiz)
{
    return (*env)->GetLongField(env, thiz, g_clazz.field_mNativeFFmpegCommand);
}

static void jni_nativeXMFFmpegCommand_set(JNIEnv *env, jobject thiz, jlong value)
{
    (*env)->SetLongField(env, thiz, g_clazz.field_mNativeFFmpegCommand, value);
}

inline static void
post_event(JNIEnv *env, jobject weak_this, int what, int arg1, int arg2)
{
    (*env)->CallStaticVoidMethod(env, g_clazz.clazz, g_clazz.method_postEventFromNative, weak_this, what, arg1, arg2, NULL);
}

static XMFFmpegCmd *jni_get_ffmpeg_cmd(JNIEnv* env, jobject thiz)
{
    pthread_mutex_lock(&g_clazz.mutex);

    XMFFmpegCmd *cmd = (XMFFmpegCmd *) (intptr_t) jni_nativeXMFFmpegCommand_get(env, thiz);
    if (cmd) {
        ffcmd_inc_ref(cmd);
    }

    pthread_mutex_unlock(&g_clazz.mutex);
    return cmd;
}

static XMFFmpegCmd *jni_set_ffmpeg_cmd(JNIEnv* env, jobject thiz, XMFFmpegCmd *cmd)
{
    pthread_mutex_lock(&g_clazz.mutex);

    XMFFmpegCmd *oldcmd = (XMFFmpegCmd*) (intptr_t) jni_nativeXMFFmpegCommand_get(env, thiz);
    if (cmd) {
        ffcmd_inc_ref(cmd);
    }
    jni_nativeXMFFmpegCommand_set(env, thiz, (intptr_t)cmd);

    pthread_mutex_unlock(&g_clazz.mutex);

    // NOTE: xmmr_dec_ref may block thread
    if (oldcmd != NULL) {
        ffcmd_dec_ref_p(&oldcmd);
    }

    return oldcmd;
}

static void message_loop_n(JNIEnv *env, XMFFmpegCmd *cmd)
{
    jobject weak_thiz = (jobject) ffcmd_get_weak_thiz(cmd);
    JNI_CHECK_GOTO(weak_thiz, env, NULL, "ffcmd jni: message_loop_n: null weak_thiz", LABEL_RETURN);

    while (1) {
        AVMessage msg;

        int retval = ffcmd_get_msg(cmd, &msg, 1);
        if (retval < 0)
            break;

        // block-get should never return 0
        assert(retval > 0);

        switch (msg.what) {
        case FFP_MSG_FLUSH:
            LOGD("FFCMD_NOP\n");
            post_event(env, weak_thiz, FFCMD_NOP, 0, 0);
            break;
        case FFCMD_MSG_ERROR:
            LOGD("FFCMD_ERROR: 0x%x\n", msg.arg1);
            post_event(env, weak_thiz, FFCMD_ERROR, msg.arg1, msg.arg2);
            break;
        case FFCMD_INFO_PREPARED:
        case FFCMD_INFO_STARTED:
        case FFCMD_INFO_PROGRESS:
        case FFCMD_INFO_STOPPED:
        case FFCMD_INFO_COMPLETED:
            //LOGD("FFCMD_INFO: 0x%x\n", msg.what);
            post_event(env, weak_thiz, FFCMD_INFO, msg.what, msg.arg1);
            break;
        case FFCMD_MSG_STATE_CHANGED:
            break;
        default:
            ALOGE("unknown FFCMD_MSG_xxx(%d)\n", msg.what);
            break;
        }
    }

LABEL_RETURN:
    ;
}

static int message_loop(void *arg)
{
    LOGD("%s\n", __func__);

    JNIEnv *env = NULL;
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL );

    XMFFmpegCmd *cmd = (XMFFmpegCmd*) arg;
    JNI_CHECK_GOTO(cmd, env, NULL, "ffcmd jni: message_loop: null cmd", LABEL_RETURN);

    message_loop_n(env, cmd);

LABEL_RETURN:
    //inc when msg_loop thread is created
    ffcmd_dec_ref_p(&cmd);
    (*g_jvm)->DetachCurrentThread(g_jvm);

    LOGD("message_loop exit");
    return 0;
}

static void
XMFFmpegCommand_release(JNIEnv *env, jobject thiz)
{
    LOGD("%s\n", __func__);

    XMFFmpegCmd *cmd = jni_get_ffmpeg_cmd(env, thiz);
    //JNI_CHECK_GOTO(cmd, env, "java/lang/IllegalStateException", "ffcmd jni: release: null cmd", LABEL_RETURN);
    if(cmd == NULL) {
        LOGI("XMFFmpegCommand_release cmd is NULL\n");
        goto LABEL_RETURN;
    }

    xm_ffmpeg_cmd_msg_thread_exit(cmd);
    (*env)->DeleteGlobalRef(env, (jobject)ffcmd_set_weak_thiz(cmd, NULL));
    jni_set_ffmpeg_cmd(env, thiz, NULL);
LABEL_RETURN:
    ffcmd_dec_ref_p(&cmd);
}

static void
XMFFmpegCommand_native_finalize(JNIEnv * env,jobject thiz)
{
    XMFFmpegCommand_release(env, thiz);
}

static void
XMFFmpegCommand_stop(JNIEnv *env, jobject thiz)
{
    LOGD("%s\n", __func__);

    XMFFmpegCmd *cmd = jni_get_ffmpeg_cmd(env, thiz);
    if (cmd != NULL)
        xm_ffmpeg_cmd_stop(cmd);

    ffcmd_dec_ref_p(&cmd);
}

static void
XMFFmpegCommand_start(JNIEnv *env, jobject thiz, jint cmdnum, jobjectArray cmdline)
{
    LOGD("%s\n", __func__);
    int argc = cmdnum;
    if (argc <= 0)
        return;

    char **argv = (char**)av_mallocz(sizeof(char*) * argc);
    for (int i = 0; i < argc; i++) {
        jstring string = (*env)->GetObjectArrayElement(env, cmdline, i);
        const char *tmp = (*env)->GetStringUTFChars(env, string, 0);
        if (NULL == tmp)
            goto end;
        argv[i] = av_strdup(tmp);
        (*env)->ReleaseStringUTFChars(env, string, tmp);
        (*env)->DeleteLocalRef(env, string);
    }

    XMFFmpegCmd *cmd = jni_get_ffmpeg_cmd(env, thiz);
    JNI_CHECK_GOTO(cmd, env, "java/lang/IllegalStateException", "ffcmd jni: start: null cmd", LABEL_RETURN);
    xm_ffmpeg_cmd_start(cmd, argc, argv);
LABEL_RETURN:
    ffcmd_dec_ref_p(&cmd);
end:
    for (int i=0; i < argc; i++) {
        av_free(argv[i]);
    }
    av_free(argv);
}

static jint
XMFFmpegCommand_prepareAsync(JNIEnv *env, jobject thiz)
{
    LOGD("%s\n", __func__);
    jint ret = 0;

    XMFFmpegCmd *cmd = jni_get_ffmpeg_cmd(env, thiz);
    JNI_CHECK_GOTO(cmd, env, "java/lang/IllegalStateException", "ffcmd jni: prepare: null cmd", LABEL_RETURN);

    ret = xm_ffmpeg_cmd_prepareAsync(cmd);
LABEL_RETURN:
    ffcmd_dec_ref_p(&cmd);
    return ret;
}

static void
XMFFmpegCommand_native_setup(JNIEnv *env, jobject thiz, jobject weak_this)
{
    LOGD("%s\n", __func__);
    XMFFmpegCmd *cmd = xm_ffmpeg_cmd_create(message_loop);
    JNI_CHECK_GOTO(cmd, env, "java/lang/OutOfMemoryError", "ffcmd jni: native_setup: ffcmd_create() failed", LABEL_RETURN);

    jni_set_ffmpeg_cmd(env, thiz, cmd);
    ffcmd_set_weak_thiz(cmd, (*env)->NewGlobalRef(env, weak_this));
LABEL_RETURN:
    ffcmd_dec_ref_p(&cmd);
}

static void
XMFFmpegCommand_native_setLogLevel(JNIEnv *env, jobject thiz, jint level)
{
    LOGD("%s(%d)\n", __func__, level);
    xm_ffmpeg_set_log_level(level);
}

static JNINativeMethod g_methods[] = {
    { "native_setLogLevel",        "(I)V",   (void *) XMFFmpegCommand_native_setLogLevel },
    { "native_setup",        "(Ljava/lang/Object;)V",   (void *) XMFFmpegCommand_native_setup },
    { "native_prepareAsync", "()I",                     (void *) XMFFmpegCommand_prepareAsync},
    { "native_start",        "(I[Ljava/lang/String;)V", (void *) XMFFmpegCommand_start},
    { "native_stop",         "()V",                     (void *) XMFFmpegCommand_stop },
    { "native_finalize",     "()V",                     (void *) XMFFmpegCommand_native_finalize },
    { "native_release",      "()V",                     (void *) XMFFmpegCommand_release },
};

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv* env = NULL;

    g_jvm = vm;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        return -1;
    }

    if(env == NULL)
        return -1;

    pthread_mutex_init(&g_clazz.mutex, NULL );

    IJK_FIND_JAVA_CLASS(env, g_clazz.clazz, JNI_CLASS_XM_FFMPEG_COMMAND);
    (*env)->RegisterNatives(env, g_clazz.clazz, g_methods, NELEM(g_methods));

    g_clazz.field_mNativeFFmpegCommand = (*env)->GetFieldID(env, g_clazz.clazz, "mNativeFFmpegCommand", "J");

    g_clazz.method_postEventFromNative = (*env)->GetStaticMethodID(env, g_clazz.clazz, "postEventFromNative", "(Ljava/lang/Object;IIILjava/lang/Object;)V");

    xm_ffmpeg_init();

    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *jvm, void *reserved)
{
    pthread_mutex_destroy(&g_clazz.mutex);
}

