#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
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

  // get streams and setup variables for video and audio
  avformat_find_stream_info(format_ctx, NULL);
  int video_idx = -1;
  int audio_idx = -1;
  const AVCodec* video_codec = NULL;
  AVCodecParameters* video_codecpar = NULL;
  const AVCodec* audio_codec = NULL;
  AVCodecParameters* audio_codecpar = NULL;

  // find information for the video and audio streams
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
      continue;
    }

    // detect if it is an audio stream and set info
    if (media_type == AVMEDIA_TYPE_AUDIO) {
      audio_codec = avcodec_find_decoder(par->codec_id);
      audio_codecpar = par;
      audio_idx = i;
      continue;
    }
  }

  // throw an error if no video stream is detected
  if (video_idx == -1 || video_codec == NULL) {
    printf("Error: file must be a video file\n");
    return -1;
  }

  // throw an error if no audio stream is detected
  if (audio_idx == -1 || audio_codec == NULL) {
    printf("Error: file does not have audio\n");
    return -1;
  }
  
  // initialize SDL
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // create SDL window
  SDL_Window* window = SDL_CreateWindow("visage",
                                        video_codecpar->width, video_codecpar->height,
                                        SDL_WINDOW_RESIZABLE);
  if (!window) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // create audio specifications for SDL
  SDL_AudioSpec audiospec = {0};
  audiospec.channels = audio_codecpar->ch_layout.nb_channels;
  audiospec.format = SDL_AUDIO_S16;
  audiospec.freq = audio_codecpar->sample_rate;

  // allocate memory for SWR conversion context and set parameters
  SwrContext* swr_ctx = NULL;
  int r = swr_alloc_set_opts2(&swr_ctx, &audio_codecpar->ch_layout, AV_SAMPLE_FMT_S16,
                              audio_codecpar->sample_rate, &audio_codecpar->ch_layout,
                              audio_codecpar->format, audio_codecpar->sample_rate, 0, NULL);
  if (r < 0) {
    printf("Error: failed to allocate audio conversion context\n");
    return -1;
  }

  // initialize SWR conversion context
  if (swr_init(swr_ctx) < 0) {
    printf("Error: unable to initialize audio conversion context\n");
    return -1;
  }

  // open and start the SDL audio device stream
  SDL_AudioStream* audiostream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                           &audiospec, NULL, NULL);
  if (!audiostream) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }
  SDL_ResumeAudioStreamDevice(audiostream);

  // initialize SDL renderer
  SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
  if (!renderer) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // create texture to render the video
  SDL_Texture* video_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                                 SDL_TEXTUREACCESS_STREAMING,
                                                 video_codecpar->width,
                                                 video_codecpar->height);

  // initialize the SWS conversion context
  struct SwsContext* sws_ctx = sws_getContext(video_codecpar->width, video_codecpar->height,
                                              video_codecpar->format, video_codecpar->width,
                                              video_codecpar->height, AV_PIX_FMT_YUV420P,
                                              SWS_BILINEAR, NULL, NULL, NULL);

  // allocate memory for the audio codec context
  AVCodecContext* audio_codec_ctx = avcodec_alloc_context3(audio_codec);
  if (!audio_codec_ctx) {
    printf("Error: failed to allocate memory for audio codec context\n");
    return -1;
  }

  // copy codec parameters to the video context
  if (avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar) < 0) {
    printf("Error: failed to copy codec parameters to the audio context\n");
    return -1;
  }

  // initialize the video codec context to use the given decoder
  if (avcodec_open2(audio_codec_ctx, audio_codec, NULL) < 0) {
    printf("Error: failed to initialize audio codec context\n");
    return -1;
  }

  // allocate memory for the video codec context
  AVCodecContext* video_codec_ctx = avcodec_alloc_context3(video_codec);
  if (!video_codec_ctx) {
    printf("Error: failed to allocate memory for video codec context\n");
    return -1;
  }

  // copy codec parameters to the video context
  if (avcodec_parameters_to_context(video_codec_ctx, video_codecpar) < 0) {
    printf("Error: failed to copy codec parameters to the video context\n");
    return -1;
  }

  // initialize the video codec context to use the given decoder
  if (avcodec_open2(video_codec_ctx, video_codec, NULL) < 0) {
    printf("Error: failed to initialize video codec context\n");
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
  scaled_frame->width = video_codecpar->width;
  scaled_frame->height = video_codecpar->height;

  // allocate new buffer for scaled frames
  int ret = av_frame_get_buffer(scaled_frame, 0);
  if (ret < 0) {
    printf("Error: %s\n", av_err2str(ret));
    return -1;
  }

  // start SDl event loop
  SDL_Event event;

  // read frames from the stream
  while (av_read_frame(format_ctx, packet) >= 0) {
    // poll SDL events
    SDL_PollEvent(&event);

    // handle SDL events
    switch (event.type) {
    case SDL_EVENT_QUIT:
      goto cleanup;
    }

    // ensure it is an audio or video frame
    if (packet->stream_index != video_idx && packet->stream_index != audio_idx) continue;

    // handle if it is a video frame
    if (packet->stream_index == video_idx) {
      // send packet to the decoder
      int send_ret = avcodec_send_packet(video_codec_ctx, packet);
      av_packet_unref(packet);
      if (send_ret < 0) {
        printf("Error: %s\n", av_err2str(send_ret));
        return -1;
      }
    
      // receive frame from the decoder
      while (avcodec_receive_frame(video_codec_ctx, frame) >= 0) {
        // convert the frame into the YUV format
        sws_scale(sws_ctx, (const uint8_t *const *) frame->data, frame->linesize,
                  0, frame->height, scaled_frame->data, scaled_frame->linesize);

        // update texture with new frame data
        SDL_UpdateYUVTexture(video_texture, NULL,
                             scaled_frame->data[0], scaled_frame->linesize[0],
                             scaled_frame->data[1], scaled_frame->linesize[1],
                             scaled_frame->data[2], scaled_frame->linesize[2]);

        // clear current renderer and copy new texture
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, video_texture, NULL, NULL);
        SDL_RenderPresent(renderer);
      }
    }
    
    // handle if it is an audio frame
    else if (packet->stream_index == audio_idx) {
      // send packet to the decoder
      int send_ret = avcodec_send_packet(audio_codec_ctx, packet);
      av_packet_unref(packet);
      if (send_ret < 0) {
        printf("Error: %s\n", av_err2str(send_ret));
        return -1;
      }
      
      while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
        // allocate output sample buffer
        uint8_t* buffer;
        av_samples_alloc(&buffer, NULL, audiospec.channels, frame->nb_samples,
                         AV_SAMPLE_FMT_S16, 0);

        // convert to S16 format
        swr_convert(swr_ctx, &buffer, frame->nb_samples, (const uint8_t**)frame->data,
                    frame->nb_samples);
            
        // put samples to the audio stream
        SDL_PutAudioStreamData(audiostream, buffer,
                               frame->nb_samples * audiospec.channels * sizeof(int16_t));

        // free the buffer
        av_freep(&buffer);
      }
    }
  }

  // cleanup everything
 cleanup:
  av_frame_free(&scaled_frame);
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&video_codec_ctx);
  avcodec_free_context(&audio_codec_ctx);
  sws_freeContext(sws_ctx);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyAudioStream(audiostream);
  swr_free(&swr_ctx);
  SDL_Quit();
  avformat_free_context(format_ctx);

  return 0;
}
