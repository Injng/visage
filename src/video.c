#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
#include <pthread.h>

/**
 * Holds the queue of video frames.
 */
typedef struct VisageVideoFrames {
  /** The video frame. */
  AVFrame* frame;

  /** The next video frame. */
  struct VisageVideoFrames* next;

  /** The PTS in milliseconds. */
  uint64_t pts;
} VisageVideoFrames;

/** Allocates and initializes the video frames queue. Returns NULL on failure. */
VisageVideoFrames* visage_alloc_frames() {
  VisageVideoFrames* video_frames = av_mallocz(sizeof(VisageVideoFrames));
  if (!video_frames) return NULL;

  // initialize properties to null
  video_frames->frame = NULL;
  video_frames->next = NULL;
  video_frames->pts = 0;

  return video_frames;
}

/** Frees the space allocated by a frames node. */
void visage_free_frames(VisageVideoFrames* frames) {
  av_frame_free(&frames->frame);
  av_free(frames);
}

/** Frees all the frames in the queue. */
void visage_free_all_frames(VisageVideoFrames* frames) {
  while (frames) {
    VisageVideoFrames *next = frames->next;
    av_frame_free(&frames->frame);
    av_free(frames);
    frames = next;
  }
}

/**
 * Holds the context for playing the video on the screen.
 */
typedef struct VisageVideo {
  /** The format context that holds the file. */
  AVFormatContext* format_ctx;

  /** The codec the video is encoded in. */
  const AVCodec* codec;

  /** The codec parameters for the video. */
  AVCodecParameters* codecpar;

  /** The SWS context for converting the video frame pixels. */
  struct SwsContext* sws_ctx;

  /** The codec context for the video. */
  AVCodecContext* codec_ctx;

  /** The queue of video frames. */
  VisageVideoFrames* frames;

  /** Mutex for accessing the frames. */
  pthread_mutex_t frame_mutex;

  /** The index of the video stream in the format context. */
  int stream_idx;
} VisageVideo;

/** Pops a frame off of the queue. Returns NULL if queue is empty. */
AVFrame* visage_pop_video(VisageVideo* video) {
  // check if queue is empty
  pthread_mutex_lock(&video->frame_mutex);
  if (!video->frames) {
    pthread_mutex_unlock(&video->frame_mutex);
    return NULL;
  }

  // if not, pop a frame off the queue
  AVFrame* top_frame = av_frame_clone(video->frames->frame);

  // free the video frame
  VisageVideoFrames* next_frames = video->frames->next;
  visage_free_frames(video->frames);

  // update the top frame in the video context
  video->frames = next_frames;
  pthread_mutex_unlock(&video->frame_mutex);

  return top_frame;
}

/** Processes the video frames into the queue. */
int visage_process_video(VisageVideo* video) {
  // check if context is initialized
  if (!video) {
    printf("Error: visage video context is not initialized\n");
    return -1;
  }
  
  // allocate memory for packets
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    printf("Error: failed to allocate memory for packets\n");
    return -1;
  }

  // allocate memory for frames
  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    printf("Error: failed to allocate memory for frames\n");
    return -1;
  }
  
  // allocate memory for scaled frame
  AVFrame* scaled_frame = av_frame_alloc();
  if (!scaled_frame) {
    printf("Error: failed to allocate memory for scaled frames\n");
    return -1;
  }
  
  // set scaled frame parameters for YUV format
  scaled_frame->format = AV_PIX_FMT_YUV420P;
  scaled_frame->width = video->codecpar->width;
  scaled_frame->height = video->codecpar->height;

  // allocate new buffer for scaled frames
  int ret = av_frame_get_buffer(scaled_frame, 0);
  if (ret < 0) {
    printf("Error: %s\n", av_err2str(ret));
    return -1;
  }

  // read frames from the stream
  while (av_read_frame(video->format_ctx, packet) >= 0) {
    // ensure it is an audio or video fram
    if (packet->stream_index != video->stream_idx) continue;

    // send packet to the decoder
    int send_ret = avcodec_send_packet(video->codec_ctx, packet);
    av_packet_unref(packet);
    if (send_ret < 0) {
      printf("Error: %s\n", av_err2str(send_ret));
      goto cleanup;
    }
    
    // receive frame from the decoder
    while (avcodec_receive_frame(video->codec_ctx, frame) >= 0) {
      // convert the frame into the YUV format
      sws_scale(video->sws_ctx, (const uint8_t *const *) frame->data, frame->linesize,
                0, frame->height, scaled_frame->data, scaled_frame->linesize);

      // create new frame in the queue
      VisageVideoFrames* new_frame = visage_alloc_frames();

      // copy the video frame into the queue
      new_frame->frame = av_frame_clone(scaled_frame);

      // set PTS for video
      new_frame->pts = (frame->pts)
        * av_q2d(video->format_ctx->streams[video->stream_idx]->time_base) * 1000;

      // add to the queue
      pthread_mutex_lock(&video->frame_mutex);
      if (!video->frames) {
        video->frames = new_frame;
      } else {
        VisageVideoFrames *cur = video->frames;
        while (cur->next) cur = cur->next;
        cur->next = new_frame;
      }
      pthread_mutex_unlock(&video->frame_mutex);
    }
  }
    
  // cleanup everything
 cleanup:
  av_frame_free(&scaled_frame);
  av_frame_free(&frame);
  av_packet_free(&packet);
  
  return 0;
 }

/** Frees the video context. */
void visage_free_video(VisageVideo** video) {
    if (!*video) return;
    
    avcodec_free_context(&(*video)->codec_ctx);
    sws_freeContext((*video)->sws_ctx);
    visage_free_all_frames((*video)->frames);
    pthread_mutex_destroy(&(*video)->frame_mutex);
    av_free(*video);
    *video = NULL;
} 

/** Allocates and initializes the video context. Returns NULL on failure. */
VisageVideo* visage_alloc_video() {
    VisageVideo* video = av_mallocz(sizeof(VisageVideo));
    if (!video) return NULL;
    
    // initialize properties to null
    video->format_ctx = NULL;
    video->codec = NULL;
    video->codecpar = NULL;
    video->sws_ctx = NULL;
    video->codec_ctx = NULL;
    video->frames = NULL;
    pthread_mutex_init(&video->frame_mutex, NULL);
    
    return video;
}

/** Initializes the video context for Visage. Outputs 0 on success, -1 on error. */
int visage_init_video(AVFormatContext* format_ctx, VisageVideo* video) {
  // set the format context
  video->format_ctx = format_ctx;

  // temporary values for stream information
  int video_idx = -1;
  const AVCodec* video_codec = NULL;
  AVCodecParameters* video_codecpar = NULL;

  // use the context to find information for the video stream
  for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    // get stream and codec information
    AVStream* stream = format_ctx->streams[i];
    AVCodecParameters* par = stream->codecpar;
    enum AVMediaType media_type = par->codec_type;

    // detect if it is a video stream and set info
    if (media_type == AVMEDIA_TYPE_VIDEO) {
      video_codec = avcodec_find_decoder(par->codec_id);
      video_codecpar = par;
      video_idx = i;
      break;
    }
  }

  // throw an error if no video stream is detected
  if (video_idx == -1 || video_codec == NULL) {
    printf("Error: file must be a video file\n");
    return -1;
  }

  // otherwise, set values
  video->codec = video_codec;
  video->codecpar = video_codecpar;
  video->stream_idx = video_idx;

  // initialize SWS conversion context
  video->sws_ctx = sws_getContext(video_codecpar->width, video_codecpar->height,
                                              video_codecpar->format, video_codecpar->width,
                                              video_codecpar->height, AV_PIX_FMT_YUV420P,
                                              SWS_BILINEAR, NULL, NULL, NULL);
  if (!video->sws_ctx) {
    printf("Error: failed to create SWS conversion context\n");
    return -1;
  }

  // allocate memory for the video codec context
  video->codec_ctx = avcodec_alloc_context3(video_codec);
  if (!video->codec_ctx) {
    printf("Error: failed to allocate memory for video codec context\n");
    return -1;
  }

  // copy codec parameters to the video context
  if (avcodec_parameters_to_context(video->codec_ctx, video_codecpar) < 0) {
    printf("Error: failed to copy codec parameters to the video context\n");
    return -1;
  }

  // initialize the video codec context to use the given decoder
  if (avcodec_open2(video->codec_ctx, video_codec, NULL) < 0) {
    printf("Error: failed to initialize video codec context\n");
    return -1;
  }

  // initialize frames to null
  video->frames = NULL;

  return 0;
}
