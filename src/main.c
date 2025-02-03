#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
  // ensure that a file is passed into the program
  if (argc < 2) {
    char program = *argv[0];
    printf("Usage: %s <file>", &program);
    return 1;
  }

  char* file = argv[1];
  AVFormatContext* format_ctx = NULL;
  if (avformat_open_input(&format_ctx, file, NULL, NULL) != 0) {
    printf("Error");
    return 1;
  }
  
  return 0;
}
