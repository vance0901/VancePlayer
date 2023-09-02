#include "VancePlayer.h"

VancePlayer::VancePlayer(const char *data_source, JNICallbakcHelper *helper) {
    // this->data_source = data_source;
    // 如果被释放，会造成悬空指针

    // 深拷贝
    // this->data_source = new char[strlen(data_source)];
    // Java: demo.mp4
    // C层：demo.mp4\0  C层会自动 + \0,  strlen不计算\0的长度，所以我们需要手动加 \0

    this->data_source = new char[strlen(data_source) + 1];
    strcpy(this->data_source, data_source); // 把源 Copy给成员

    this->helper = helper;

    pthread_mutex_init(&seek_mutex, nullptr); // TODO 增加 3.1
}

VancePlayer::~VancePlayer() {
    if (data_source) {
        delete data_source;
        data_source = nullptr;
    }

    if (helper) {
        delete helper;
        helper = nullptr;
    }

    pthread_mutex_destroy(&seek_mutex); // TODO 增加 3.1
}

// TODO >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  下面全部都是 prepare

void *task_prepare(void *args) { // 此函数和VancePlayer这个对象没有关系，你没法拿VancePlayer的私有成员

    // avformat_open_input(0, this->data_source)

    auto *player = static_cast<VancePlayer *>(args);
    player->prepare_();
    return nullptr; // 必须返回，坑，错误很难找
}

void VancePlayer::prepare_() { // 此函数 是 子线程

    // 为什么FFmpeg源码，大量使用上下文Context？
    // 答：因为FFmpeg源码是纯C的，他不像C++、Java ， 上下文的出现是为了贯彻环境，就相当于Java的this能够操作成员

    /**
     * TODO 第一步：打开媒体地址（文件路径， 直播地址rtmp）
     */
    formatContext = avformat_alloc_context();

    // 字典（键值对）
    AVDictionary *dictionary = nullptr;
    //设置超时（5秒）
    av_dict_set(&dictionary, "timeout", "5000000", 0); // 单位微妙

    /**
     * 1，AVFormatContext *
     * 2，路径 url:文件路径或直播地址
     * 3，AVInputFormat *fmt  Mac、Windows 摄像头、麦克风， 我们目前安卓用不到
     * 4，各种设置：例如：Http 连接超时， 打开rtmp的超时  AVDictionary **options
     */
    int r = avformat_open_input(&formatContext, data_source, nullptr, &dictionary);
    // 释放字典
    av_dict_free(&dictionary);
    if (r) {
        // 把错误信息反馈给Java，回调给Java  Toast【打开媒体格式失败，请检查代码】
        // TODO 新增
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_OPEN_URL);

            // char * errorInfo = av_err2str(r); // 根据你的返回值 得到错误详情
        }
        // TODO 播放器收尾 1
        avformat_close_input(&formatContext);
        return;
    }

    // 你在 xxx.mp4 能够拿到
    // 你在 xxx.flv 拿不到，是因为封装格式的原因
    // formatContext->duration;

    /**
     * TODO 第二步：查找媒体中的音视频流的信息
     */
    r = avformat_find_stream_info(formatContext, nullptr);
    if (r < 0) {
        // TODO 新增
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_FIND_STREAMS);
        }
        // TODO 播放器收尾 1
        avformat_close_input(&formatContext);
        return;
    }

    // 你在 xxx.mp4 能够拿到
    // 你在 xxx.flv 都能拿到
    // avformat_find_stream_info FFmpeg内部源码已经做（流探索）了，所以可以拿到 总时长
    this->duration = formatContext->duration / AV_TIME_BASE; // FFmpeg的单位 基本上都是  有理数(时间基)，所以你需要这样转

    AVCodecContext *codecContext = nullptr;

    /**
     * TODO 第三步：根据流信息，流的个数，用循环来找
     */
    for (int stream_index = 0; stream_index < formatContext->nb_streams; ++stream_index) {
        /**
         * TODO 第四步：获取媒体流（视频，音频）
         */
        AVStream *stream = formatContext->streams[stream_index];

        /**
         * TODO 第五步：从上面的流中 获取 编码解码的【参数】
         * 由于：后面的编码器 解码器 都需要参数（宽高 等等）
         */
        AVCodecParameters *parameters = stream->codecpar;

        /**
         * TODO 第六步：（根据上面的【参数】）获取编解码器
         */
        AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) {
            // TODO 新增
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_FIND_DECODER_FAIL);
            }
            // TODO 播放器收尾 1
            avformat_close_input(&formatContext);
        }

        /**
        * TODO 第七步：编解码器 上下文 （这个才是真正干活的）
        */
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            // TODO 新增
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_ALLOC_CODEC_CONTEXT_FAIL);
            }

            // TODO 播放器收尾 1
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);

            return;
        }

        /**
         * TODO 第八步：他目前是一张白纸（parameters copy codecContext）
         */
        r = avcodec_parameters_to_context(codecContext, parameters);
        if (r < 0) {
            // TODO 新增
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_CODEC_CONTEXT_PARAMETERS_FAIL);
            }
            // TODO 播放器收尾 1
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);
            return;
        }

        /**
         * TODO 第九步：打开解码器
         */
        r = avcodec_open2(codecContext, codec, nullptr);
        if (r) { // 非0就是true
            // TODO 新增
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_OPEN_DECODER_FAIL);
            }
            // TODO 播放器收尾 1
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
            avformat_close_input(&formatContext);
            return;
        }

        // TODO 音视频同步 2
        AVRational time_base = stream->time_base;

        /**
         * TODO 第十步：从编解码器参数中，获取流的类型 codec_type  ===  音频 视频
         */
        if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
            // 是音频
            audio_channel = new AudioChannel(stream_index, codecContext, time_base);

            // TODO 增加 2.1
            if (this->duration != 0) { // 非直播，才有意义把 JNICallbackHelper传递过去
                audio_channel->setJNICallbakcHelper(helper);
            }

        } else if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO) {

            // 虽然是视频类型，但是只有一帧封面
            if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                continue;
            }

            // TODO 音视频同步 2.2 （视频独有的 fps值）
            AVRational fps_rational = stream->avg_frame_rate;
            int fps = av_q2d(fps_rational);

            // 是视频
            video_channel = new VideoChannel(stream_index, codecContext, time_base, fps);
            video_channel->setRenderCallback(renderCallback);

            // 考虑到以后 干脆传视频一份
            // TODO 增加 2.1
            if (this->duration != 0) { // 非直播，才有意义把 JNICallbackHelper传递过去
                video_channel->setJNICallbakcHelper(helper);
            }
        }
    } // for end

    /**
     * TODO 第十一步: 如果流中 没有音频 也没有 视频 【健壮性校验】
     */
    if (!audio_channel && !video_channel) {
        // TODO 新增
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_NOMEDIA);
        }
        // TODO 播放器收尾 1
        if (codecContext) {
            avcodec_free_context(&codecContext); // 释放此上下文 avcodec 他会考虑到，你不用管*codec
        }
        avformat_close_input(&formatContext);
        return;
    }

    /**
     * TODO 第十二步：恭喜你，准备成功，我们的媒体文件 OK了，通知给上层
     */
    if (helper) { // 只要用户关闭了，就不准你回调给Java成 start播放
        helper->onPrepared(THREAD_CHILD);
    }
}

void VancePlayer::prepare() {
    // 问题：当前的prepare函数，是子线程 还是 主线程 ？
    // 答：此函数是被MainActivity的onResume调用下来的（主线程）

    // 解封装 FFmpeg来解析  data_source 可以直接解析吗？
    // 答：data_source == 文件io流，  直播网络rtmp， 所以按道理来说，会耗时，所以必须使用子线程

    // 创建子线程
    pthread_create(&pid_prepare, 0, task_prepare, this);
}

// TODO >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  下面全部都是 start

void *task_start(void *args) {
    auto *player = static_cast<VancePlayer *>(args);
    player->start_();
    return nullptr; // 必须返回，坑，错误很难找
}

// TODO  内存泄漏关键点（控制packet队列大小，等待队列中的数据被消费） 1
// 把 视频 音频 的压缩包(AVPacket *) 循环获取出来 加入到队列里面去
void VancePlayer::start_() { // 子线程
    while (isPlaying) {
        // 解决方案：视频 我不丢弃数据，等待队列中数据 被消费 内存泄漏点1.1
        if (video_channel && video_channel->packets.size() > 100) {
            av_usleep(10 * 1000); // 单位 ：microseconds 微妙 10毫秒
            continue;
        }
        // 解决方案：音频 我不丢弃数据，等待队列中数据 被消费 内存泄漏点1.2
        if (audio_channel && audio_channel->packets.size() > 100) {
            av_usleep(10 * 1000); // 单位 ：microseconds 微妙 10毫秒
            continue;
        }

        // AVPacket 可能是音频 也可能是视频（压缩包）
        AVPacket * packet = av_packet_alloc();
        int ret = av_read_frame(formatContext, packet);
        if (!ret) { // ret == 0

            // AudioChannel    队列
            // VideioChannel   队列

            // 把我们的 AVPacket* 加入队列， 音频 和 视频
            /*AudioChannel.insert(packet);
            VideioChannel.insert(packet);*/

            if (video_channel && video_channel->stream_index == packet->stream_index) {
                // 代表是视频
                video_channel->packets.insertToQueue(packet);
            } else if (audio_channel && audio_channel->stream_index == packet->stream_index) {
                // 代表是音频
                audio_channel->packets.insertToQueue(packet); // TODO 增加的
            }
        } else if (ret == AVERROR_EOF) { //   end of file == 读到文件末尾了 == AVERROR_EOF
            // TODO 1.3 内存泄漏点
            if (video_channel->packets.empty() && audio_channel->packets.empty()) {
                break; // 队列的数据被音频 视频 全部播放完毕了，我在退出
            }
        } else {
            break; // av_read_frame 出现了错误，结束当前循环
        }
    }// end while
    isPlaying = false;
    video_channel->stop();
    audio_channel->stop();
}

void VancePlayer::start() {
    isPlaying = 1;

    // 视频：1.解码    2.播放
    // 1.把队列里面的压缩包(AVPacket *)取出来，然后解码成（AVFrame * ）原始包 ----> 保存队列
    // 2.把队列里面的原始包(AVFrame *)取出来， 视频播放
    if (video_channel) {
        // TODO 音视频同步 3.1
        video_channel->setAudioChannel(audio_channel);
        video_channel->start();
    }

    // TODO 增加的
    // 视频：1.解码    2.播放
    // 1.把队列里面的压缩包(AVPacket *)取出来，然后解码成（AVFrame * ）原始包 ----> 保存队列
    // 2.把队列里面的原始包(AVFrame *)取出来， 音频播放
    if (audio_channel) {
        audio_channel->start();
    }

    // 把 音频和视频 压缩包 加入队列里面去
    // 创建子线程
    pthread_create(&pid_start, 0, task_start, this);
}

void VancePlayer::setRenderCallback(RenderCallback renderCallback) {
    this->renderCallback = renderCallback;
}

// TODO 增加 获取总时长
int VancePlayer::getDuration() {
    return duration; // 在调用此函数之前，必须给此duration变量赋值
}

void VancePlayer::seek(int progress) {

    // 健壮性判断
    if (progress < 0 || progress > duration) {
        return;
    }
    if (!audio_channel && !video_channel) {
        return;
    }
    if (!formatContext) {
        return;
    }

    // formatContext 多线程， av_seek_frame内部会对我们的 formatContext上下文的成员做处理，安全的问题
    // 互斥锁 保证多线程情况下安全

    pthread_mutex_lock(&seek_mutex);

    // FFmpeg 大部分单位 == 时间基AV_TIME_BASE
    /**
     * 1.formatContext 安全问题
     * 2.-1 代表默认情况，FFmpeg自动选择 音频 还是 视频 做 seek，  模糊：0视频  1音频
     * 3. AVSEEK_FLAG_ANY（老实） 直接精准到 拖动的位置，问题：如果不是关键帧，B帧 可能会造成 花屏情况
     *    AVSEEK_FLAG_BACKWARD（则优  8的位置 B帧 ， 找附件的关键帧 6，如果找不到他也会花屏）
     *    AVSEEK_FLAG_FRAME 找关键帧（非常不准确，可能会跳的太多），一般不会直接用，但是会配合用
     */
     int r = av_seek_frame(formatContext, -1, progress * AV_TIME_BASE, AVSEEK_FLAG_FRAME);
     if (r < 0) {
         return;
     }

     // TODO 如果你的视频，假设出了花屏，AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME， 缺点：慢一些
     // 有一点点冲突，后面再看 （则优  | 配合找关键帧）
     // av_seek_frame(formatContext, -1, progress * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME);

     // 音视频正在播放，用户去 seek，我是不是应该停掉播放的数据  音频1frames 1packets，  视频1frames 1packets 队列

     // 这四个队列，还在工作中，让他们停下来， seek完成后，重新播放
     if (audio_channel) {
         audio_channel->packets.setWork(0);  // 队列不工作
         audio_channel->frames.setWork(0);  // 队列不工作
         audio_channel->packets.clear();
         audio_channel->frames.clear();
         audio_channel->packets.setWork(1); // 队列继续工作
         audio_channel->frames.setWork(1);  // 队列继续工作
     }

    if (video_channel) {
        video_channel->packets.setWork(0);  // 队列不工作
        video_channel->frames.setWork(0);  // 队列不工作
        video_channel->packets.clear();
        video_channel->frames.clear();
        video_channel->packets.setWork(1); // 队列继续工作
        video_channel->frames.setWork(1);  // 队列继续工作
    }

    pthread_mutex_unlock(&seek_mutex);

}

void *task_stop(void *args) {
    auto *player = static_cast<VancePlayer *>(args);
    player->stop_(player);
    return nullptr; // 必须返回，坑，错误很难找
}

void VancePlayer::stop_(VancePlayer * vancePlayer) {
    isPlaying = false;
    pthread_join(pid_prepare, nullptr);
    pthread_join(pid_start, nullptr);

    // pid_prepare pid_start 就全部停止下来了  稳稳的停下来
    if (formatContext) {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
    DELETE(audio_channel);
    DELETE(video_channel);
    DELETE(vancePlayer);
}

void VancePlayer::stop() {

    // 只要用户关闭了，就不准你回调给Java成 start播放
    helper = nullptr;
    if (audio_channel) {
        audio_channel->jniCallbakcHelper = nullptr;
    }
    if (video_channel) {
        video_channel->jniCallbakcHelper = nullptr;
    }


    // 如果是直接释放 我们的 prepare_ start_ 线程，不能暴力释放 ，否则会有bug

    // 让他 稳稳的停下来

    // 我们要等这两个线程 稳稳的停下来后，我再释放VancePlayer的所以工作
    // 由于我们要等 所以会ANR异常

    // 所以我们我们在开启一个 stop_线程 来等你 稳稳的停下来
    // 创建子线程
    pthread_create(&pid_stop, nullptr, task_stop, this);
}







