#ifndef VISAGE_VIDEO_H
#define VISAGE_VIDEO_H

#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <stdint.h>
#include <pthread.h>

struct SwsContext;
struct AVCodecContext;
struct AVCodecParameters;

/**
 * Structure representing a node in the video frame queue.
 * 
 * This structure holds decoded video frames in a linked list format, allowing
 * for frame-by-frame playback and synchronized video rendering. Each node
 * contains a single frame of video data, its presentation timestamp, and a
 * pointer to the next frame in the queue.
 *
 * The frame data is stored in an AVFrame structure which must be allocated
 * using av_frame_alloc() and freed with av_frame_free(). The frame data
 * itself is reference counted through the AVBuffer API.
 */
typedef struct VisageVideoFrames {
    /**
     * Pointer to the actual video frame data.
     * Contains the decoded video frame in YUV420P format.
     * Must be allocated with av_frame_alloc() and freed with av_frame_free().
     */
    AVFrame* frame;

    /**
     * Pointer to the next frame in the queue.
     * NULL if this is the last frame in the queue.
     */
    struct VisageVideoFrames* next;

    /**
     * Presentation timestamp in milliseconds.
     * Represents when this frame should be displayed relative to the
     * video start time.
     */
    uint64_t pts;
} VisageVideoFrames;

/**
 * Main structure for handling video playback.
 *
 * This structure contains all the necessary contexts and parameters for
 * decoding and processing a video stream. It manages the codec state,
 * frame conversion, and maintains a thread-safe queue of decoded frames
 * ready for display.
 *
 * The structure must be allocated using visage_alloc_video() and initialized
 * with visage_init_video(). When no longer needed, it should be freed using
 * visage_free_video().
 *
 * Thread safety: The frame queue is protected by a mutex to allow concurrent
 * access from decoding and rendering threads. All other fields should only
 * be accessed from a single thread.
 */
typedef struct VisageVideo {
    /**
     * Format context containing the opened video file information.
     * Set during initialization and used throughout video processing.
     */
    AVFormatContext* format_ctx;

    /**
     * Codec used for decoding the video stream.
     * Found automatically based on the video stream's codec ID.
     */
    const AVCodec* codec;

    /**
     * Parameters describing the video codec's properties.
     * Contains information like dimensions, pixel format, etc.
     */
    AVCodecParameters* codecpar;

    /**
     * Software scaling context for pixel format conversion.
     * Used to convert decoded frames to YUV420P format.
     */
    struct SwsContext* sws_ctx;

    /**
     * Context for the initialized video codec.
     * Contains the state of the decoder during video processing.
     */
    AVCodecContext* codec_ctx;

    /**
     * Queue of decoded video frames ready for display.
     * Protected by frame_mutex for thread-safe access.
     */
    VisageVideoFrames* frames;

    /**
     * Mutex protecting access to the frames queue.
     * Must be locked when modifying or accessing the frames field.
     */
    pthread_mutex_t frame_mutex;

    /**
     * Index of the video stream in the format context.
     * Used to identify video packets during processing.
     */
    int stream_idx;
} VisageVideo;

/**
 * Allocates a new video frame queue node.
 *
 * The allocated node has all fields initialized to NULL/0. The caller is
 * responsible for setting the frame data and linking it into the queue.
 *
 * @return Newly allocated VisageVideoFrames node, or NULL on allocation failure
 */
VisageVideoFrames* visage_alloc_frames();

/**
 * Frees a single video frame queue node.
 *
 * This function frees both the frame data (using av_frame_free) and the
 * node structure itself. Does not affect other nodes in the queue.
 *
 * @param frames Node to free, must not be NULL
 */
void visage_free_frames(VisageVideoFrames* frames);

/**
 * Frees an entire queue of video frames.
 *
 * Traverses the linked list starting at the given node and frees all
 * subsequent nodes and their associated frame data.
 *
 * @param frames First node in the queue to free, can be NULL
 */
void visage_free_all_frames(VisageVideoFrames* frames);

/**
 * Allocates and initializes a new video context.
 *
 * The allocated context has all fields initialized to NULL/0 except for
 * the frame mutex which is initialized for immediate use.
 *
 * @return Newly allocated VisageVideo context, or NULL on allocation failure
 */
VisageVideo* visage_alloc_video();

/**
 * Initializes a video context for playback.
 *
 * This function:
 * - Locates the video stream in the format context
 * - Sets up the appropriate decoder
 * - Initializes scaling context for YUV420P conversion
 * - Prepares the context for frame processing
 *
 * @param format_ctx Opened format context containing the video stream
 * @param video Video context to initialize
 * @return 0 on success, -1 on error with error message printed to stdout
 */
int visage_init_video(AVFormatContext* format_ctx, VisageVideo* video);

/**
 * Frees all resources associated with a video context.
 *
 * This function:
 * - Frees all decoded frames in the queue
 * - Releases codec contexts and scaling contexts
 * - Destroys the frame mutex
 * - Frees the video context structure itself
 *
 * @param video Pointer to the video context pointer, will be set to NULL
 */
void visage_free_video(VisageVideo** video);

/**
 * Removes and returns the next frame from the video queue.
 *
 * This function is thread-safe and will lock the frame mutex while
 * accessing the queue. The returned frame must be freed by the caller
 * using av_frame_free().
 *
 * @param video Video context containing the frame queue
 * @return Copy of the next frame in the queue, or NULL if queue is empty
 */
AVFrame* visage_pop_video(VisageVideo* video);

/**
 * Processes the video stream and fills the frame queue.
 *
 * This function:
 * - Reads packets from the video stream
 * - Decodes them into raw frames
 * - Converts frames to YUV420P format
 * - Adds them to the frame queue in a thread-safe manner
 *
 * The frames are added to the queue with proper PTS values for
 * synchronized playback.
 *
 * @param video Initialized video context
 * @return 0 on success, -1 on error with error message printed to stdout
 */
int visage_process_video(VisageVideo* video);

#endif // VISAGE_VIDEO_H
