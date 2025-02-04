#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  printf("Running with %d arguments\n", argc);
  // ensure that a file is passed into the program
  if (argc < 2) {
    char* program = argv[0];
    printf("Usage: %s <file>\n", program);
    return 1;
  }

  // open the file
  char* file = argv[1];
  AVFormatContext* format_ctx = NULL;
  if (avformat_open_input(&format_ctx, file, NULL, NULL) != 0) {
    printf("Error\n");
    return 1;
  }

  // get streams and detect video stream
  avformat_find_stream_info(format_ctx, NULL);
  int video_idx = -1;
  for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    AVStream* stream = format_ctx->streams[i];
    enum AVMediaType media_type = stream->codecpar->codec_type;
    if (media_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = i;
      break;
    }
  }

  printf("Video stream index: %d", video_idx);

  /* AVCodecContext* codec_ctx = NULL; */
  /* AVPacket* packet = NULL;   */

  /* while (av_read_frame(format_ctx, packet) != 0) { */
  /*   avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt) */
  /* } */
  
  return 0;
}
