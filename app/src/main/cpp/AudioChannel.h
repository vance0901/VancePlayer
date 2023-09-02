#ifndef VANCEPLAYER_AUDIOCHANNEL_H
#define VANCEPLAYER_AUDIOCHANNEL_H

#include "BaseChannel.h"
#include "JNICallbakcHelper.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern "C" {
#include <libswresample/swresample.h> // 对音频数据进行转换（重采样）
}

class AudioChannel : public BaseChannel {

private:
    pthread_t pid_audio_decode;
    pthread_t pid_audio_play;

public:
    int out_channels;
    int out_sample_size;
    int out_sample_rate;
    int out_buffers_size;
    uint8_t *out_buffers = 0;
    SwrContext *swr_ctx = 0;

public:
    //引擎
    SLObjectItf engineObject = 0;
    // 引擎接口
    SLEngineItf engineInterface = 0;
    // 混音器
    SLObjectItf outputMixObject = 0;
    // 播放器
    SLObjectItf bqPlayerObject = 0;
    // 播放器接口
    SLPlayItf bqPlayerPlay = 0;

    // 播放器队列接口
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = 0;

    double audio_time; // TODO 音视频同步 1.1

public:
    AudioChannel(int stream_index, AVCodecContext *codecContext, AVRational rational);

    ~AudioChannel();

    void stop();

    void start();


    void audio_decode();

    void audio_play();


    int getPCM();
};

#endif //VANCEPLAYER_AUDIOCHANNEL_H
