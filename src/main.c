#include <ffmpeg4.4/libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  printf("Running with %d arguments\n", argc);
  // ensure that a file is passed into the program
  if (argc < 2) {
    char* program = argv[0];
    printf("Usage: %s <file>\n", program);
    return -1;
  }

  // open the file
  char* file = argv[1];
  AVFormatContext* format_ctx = NULL;
  if (avformat_open_input(&format_ctx, file, NULL, NULL) != 0) {
    printf("Error\n");
    return -1;
  }

  // get streams and detect video stream index and codec
  avformat_find_stream_info(format_ctx, NULL);
  int video_idx = -1;
  const AVCodec* codec = NULL;
  AVCodecParameters* codecpar = NULL;
  for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    // get stream and codec information
    AVStream* stream = format_ctx->streams[i];
    AVCodecParameters* par = stream->codecpar;
    enum AVMediaType media_type = par->codec_type;

    // detect if it is a video stream and set info
    if (media_type == AVMEDIA_TYPE_VIDEO) {
      codec = avcodec_find_decoder(par->codec_id);
      codecpar = par;
      video_idx = i;
      break;
    }
  }

  // throw an error if no video stream is detected
  if (video_idx == -1 || codec == NULL) {
    printf("Error: file must be a video file\n");
    return -1;
  }

  // initialize the SWS conversion context
  struct SwsContext* sws_ctx = sws_getContext(codecpar->width, codecpar->height,
                                              codecpar->format, codecpar->width,
                                              codecpar->height, AV_PIX_FMT_YUV420P,
                                              SWS_BILINEAR, NULL, NULL, NULL);

  // allocate memory for codec context
  AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    printf("Error: failed to allocate memory for codec context\n");
    return -1;
  }

  // copy codec parameters to the context
  if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
    printf("Error: failed to copy codec parameters to the context\n");
    return -1;
  }

  // initialize the codec context to use the given decoder
  if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
    printf("Error: failed to initialize codec context\n");
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
  scaled_frame->width = codecpar->width;
  scaled_frame->height = codecpar->height;

  // allocate new buffer for scaled frames
  int ret = av_frame_get_buffer(scaled_frame, 0);
  if (ret < 0) {
    printf("Error: %s\n", av_err2str(ret));
    return -1;
  }

  // read frames from the stream
  while (av_read_frame(format_ctx, packet) >= 0) {
    // ensure that it is the video frame
    if (packet->stream_index != video_idx) continue;

    // send packet to the decoder
    int send_ret = avcodec_send_packet(codec_ctx, packet);
    av_packet_unref(packet);
    if (send_ret < 0) {
      printf("Error: %s\n", av_err2str(send_ret));
      return -1;
    }

    // receive frame from the decoder
    while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
      // convert the frame into the YUV format
      sws_scale(sws_ctx, (const uint8_t *const *) frame->data, frame->linesize,
                0, frame->height, scaled_frame->data, scaled_frame->linesize);

      // get scaled frame data
      printf("Frame sample rate: %d\n", scaled_frame->sample_rate);
    }
  }

  // cleanup everything
  av_frame_free(&scaled_frame);
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&codec_ctx);
  sws_freeContext(sws_ctx);
  avformat_free_context(format_ctx);

  return 0;
}
