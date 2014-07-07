/*!
 *   \file  main.c
 *   \brief  
 *  
 *  <+DETAILED+>
 *  
 *  \author  Bertrand.F (), 
 *  
 *  \internal
 *       Created:  25/06/2014
 *      Revision:  none
 *      Compiler:  gcc
 *  Organization:  
 *     Copyright:  Copyright (C), 2014, Bertrand.F
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/wait.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL/SDL.h>

#define VERSION_MAJOR   (0)
#define VERSION_MINOR   (2)

#define OPT_STRING      "hl:o:r:"
static struct option long_options[] = {
    {"help",        no_argument,        NULL,   'h'},
    {"loops",       required_argument,  NULL,   'l'},
    {"output",      required_argument,  NULL,   'o'},
    {"framerate",   required_argument,  NULL,   'r'},
    {0,             0,                  NULL,    0 }
};

#define WINDOW_NAME         "WEBCAM"

#define DEFAULT_FRAMERATE   (10)
#define DEFAULT_FRAMEWIDTH  (352)
#define DEFAULT_FRAMEHEIGHT (288)
#define DEFAULT_LOOPS       (50)
#define DEFAULT_OUTFCC      (CV_FOURCC('M', 'P', '4', 'V'))
#define DEFAULT_INFILE      "/dev/stdin"

static int  frameRate   = DEFAULT_FRAMERATE;
static struct {
    int width;
    int height;
} inSize = { DEFAULT_FRAMEWIDTH, DEFAULT_FRAMEHEIGHT };
static int  loops       = DEFAULT_LOOPS;
static char *outfile    = NULL;
static int  sleepTime   = 1000000 / DEFAULT_FRAMERATE;
static char *url        = NULL;
static int rtmpdump_pid = -1;

// AV media
static struct {
    AVFormatContext *fmtCtx;
    AVCodecContext  *codecCtx;
    AVCodec         *codec;
    int             videoStreamIdx; 
} avm;

// Signals received
/* All global variable used in signal handlers must use the volatile
 * Keyword to indicate the compiler that their value can be 
 * asynchronously changed.
 * */
volatile uint32_t sigs_received =0;
#define FLAG_SIGINT      ((uint32_t)(0x01))
#define FLAG_SIGCHLD     ((uint32_t)(0x02))
#define SET_SIG(x, y)   ( x |= y )
#define HAS_SIG(x, y)   ((x & y) != 0)


/*
 * USAGE
 *
 * Prints program's help message.
 *
 * */
void
usage(char* name)
{
    fprintf(stdout, "%s version %d.%d\n", name, VERSION_MAJOR, VERSION_MINOR);
    fprintf(stdout, "Reads video from input file and display it in a SDL window.\n");
    fprintf(stdout, "Usage: %s [options] <url>\n", name);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -h, --help            Prints this help\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "  -l, --loops <n>       Number of loops (defaults to %d)\n", DEFAULT_LOOPS);
    fprintf(stdout, "  -o, --output <file>   Save output to file\n");
    fprintf(stdout, "  -r, --framerate <n>   Set framerate (defaults to %d)\n", DEFAULT_FRAMERATE);
    fprintf(stdout, "\n");
}



/*
 * MAKE_CHILDPROCESS
 *
 * Starts RTMPDUMP and create the pipe.
 * Return 0 on success, -1 otherwise.
 *
 * */
int 
make_child_process() 
{    
    int pipefd[2];

    if(pipe(pipefd) != 0) {
        fprintf(stderr, "Cannot create pipe.\n");
        return -1;
    }

    rtmpdump_pid=fork();
    switch(rtmpdump_pid) {
        case -1: // error
            fprintf(stderr, "Fork() error.\n");
            return -1;
        case 0: // child
            close(pipefd[0]); // read end unused
            close(STDOUT_FILENO);
            dup(pipefd[1]); // pipe write end is now stdout
            close(pipefd[1]);
            execl("/usr/bin/rtmpdump", "--quiet", "--live", \
                    "--realtime", "-r", url, (char*)NULL);
            break;
        default: // parent
            close(pipefd[1]); // write end unused
            close(STDIN_FILENO);
            dup(pipefd[0]); // pipe read end is now stdin
            close(pipefd[0]);
            break;
    }
    return 0;
}



/*
 * INPUT_MEDIA_OPEN
 *
 * Opens the input media file.
 * Return 0 on success, -1 otherwise.
 *
 * */
int input_media_open() 
{    
    int i;

    // Format context creation
    avm.fmtCtx = avformat_alloc_context();
    if(!avm.fmtCtx) {
        fprintf(stderr, "Cannot create AV context.\n");
        return -1;
    }
    
    // Open input file
    if( avformat_open_input(&(avm.fmtCtx), DEFAULT_INFILE, 0, NULL)!=0 ) {
        fprintf(stderr, "Cannot open input file.\n");
        return -1;
    }

    // Retreive stream information
    if( avformat_find_stream_info(avm.fmtCtx, NULL)!=0 ) {
        fprintf(stderr, "Cannot find stream info.\n");
        return -1;
    }
    av_dump_format(avm.fmtCtx, 0, DEFAULT_INFILE, 0);

    // Find the first video stream
    for(i=0 ; i<avm.fmtCtx->nb_streams ; ++i) {
        if(avm.fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            avm.videoStreamIdx=i;
            break;
        }
    }
    if( avm.videoStreamIdx==-1 ) {
        fprintf(stderr, "Cannot find video stream.\n");
        return -1;
    }

    // Find the codec
    avm.codecCtx = avm.fmtCtx->streams[avm.videoStreamIdx]->codec;
    avm.codec = avcodec_find_decoder(avm.codecCtx->codec_id);
    if( avm.codec==NULL ) {
        fprintf(stderr, "Unsupported decoder.\n");
        return -1;
    }
    if( avcodec_open2(avm.codecCtx, avm.codec, NULL)<0 ) {
        fprintf(stderr, "Cannot open codec.\n");
        return -1;
    }
    
    return 0;
}



/*
 * INPUT_MEDIA_CLOSE
 *
 * Closes input media and resets the AV media structure.
 *
 * */
void 
input_media_close() 
{
    avcodec_close(avm.codecCtx);
    avformat_close_input(&(avm.fmtCtx));
}



/*
 * SIGCHLD_HANDLER
 *
 * Handles SIGCHLD byt setting the GOT_SIGCHLD flag. 
 *
 * */
void 
sigchld_handler(int sig) 
{
    SET_SIG(sigs_received, FLAG_SIGCHLD); 
}

/*
 * SIGINT_HANDLER
 *
 * Handles SIGINT by setting the GOT_SIGINT flag.
 *
 * */
void
sigint_handler(int sig) 
{
   SET_SIG(sigs_received, FLAG_SIGINT); 
}



/*
 * MAIN
 *
 * */
int
main(int argc, char** argv )
{
    char opt;
    int i=0, end=0, status;
    uint32_t start=0, loop_start=0;
    struct sigaction sig_act;
    
    AVPacket packet;
    AVFrame *frame=NULL;
    AVPicture pict;
    int finishedFrame;

    SDL_Event event;
    SDL_Surface *screen;
    SDL_Overlay *bmp;
    SDL_Rect rect;


    // --------------------- Parse args -----------------------------
    while( (opt=getopt_long(argc, argv, OPT_STRING, long_options, NULL))>0 ) {
        switch(opt)  {
            case 'h':
                usage(argv[0]);
                return 0;
                break;
            case 'l':
                loops = atoi(optarg);
                break;
            case 'o':
                outfile = optarg;
                break;
            case 'r':
                frameRate = atoi(optarg);
                sleepTime = 1000000 / frameRate;
                break;
            default:
                break;
        }
    }
    // Get input file
    if(optind>0 && optind<argc) {
        url = argv[optind];
    } else {
        fprintf(stderr, "Please specify streaming URL.\n");
        exit(-1);
    }
    
    // Dump configuration
    fprintf(stdout, "framerate=%d\n",   frameRate);
    fprintf(stdout, "loops=%d\n",       loops);
    fprintf(stdout, "sleeptime=%d\n",   sleepTime);
    fprintf(stdout, "URL=%s\n",         url);

    
    // --------------------- Install signal handlers ----------------
    // SIGCHLD
    if( sigaction(SIGCHLD, NULL, &sig_act)!=0 ) {
        fprintf(stderr, "Cannot get old SIGCHLD handler.\n");
        exit(-1);
    }
    sig_act.sa_flags &= ~SA_SIGINFO;
    sig_act.sa_handler = sigchld_handler;
    if( sigaction(SIGCHLD, &sig_act, NULL)!=0 ) {
        fprintf(stderr, "Cannot set custom handler for SIGCHLD.\n");
        exit(-1);
    }

    // SIGINT
    if( sigaction(SIGINT, NULL, &sig_act)!=0 ) {
        fprintf(stderr, "Cannot get old SIGINT handler.\n");
        exit(-1);
    }
    sig_act.sa_flags &= ~SA_SIGINFO;
    sig_act.sa_handler = sigint_handler;
    if( sigaction(SIGINT, &sig_act, NULL)!=0 ) {
        fprintf(stderr, "Cannot set custom handler for SIGINT.\n");
        exit(-1);
    }
    

    // --------------------- Start RTMP dump child process ----------
    if( make_child_process()!=0 ) {
        fprintf(stderr, "Could not create child process for rtmpdump.\n");
        exit(-1);
    }


    // --------------------- Configure lib AV ------------------------
    av_register_all();
    
    // Open AV media
    if( input_media_open()!=0 ) {
        exit(-1);
    }
    atexit(input_media_close);

    // Allocate space for video frame
    frame = av_frame_alloc();
    if( !frame ) {
        fprintf(stderr, "Cannot allocate video frame.\n");
        exit(-1);
    }


    // --------------------- Configure SDL --------------------------
    if( SDL_Init(SDL_INIT_VIDEO)==-1 ) {
        fprintf(stderr, "Cannot init SDL.\n");
        exit(-1);
    }
    atexit(SDL_Quit);
    screen = SDL_SetVideoMode(avm.codecCtx->width, avm.codecCtx->height, 16, SDL_HWSURFACE);
    if(screen==NULL) {
        fprintf(stderr, "Cannot create SDL surface.\n");
        exit(-1);
    }

    // Allocate place to put YUV image to display
    bmp = SDL_CreateYUVOverlay(avm.codecCtx->width, avm.codecCtx->height, 
            SDL_YV12_OVERLAY, screen);
    if( !bmp ) {
        fprintf(stderr, "Cannot create overlay.\n");
        exit(-1);
    }

    
    // --------------------- Process video frames -------------------
    rect.x = 0;
    rect.y = 0;
    rect.w = avm.codecCtx->width;
    rect.h = avm.codecCtx->height;

    /* This do{}while(...); loop catches the breaks of the for(...) loop.
     * It then checks if it breaked because of the end of the stream or 
     * because a signal was catched (SIGTERM, or SIGCHLD) and do the 
     * appropriate handling. 
     * */
    do {
        /* In this loop we retreive a frame and display it on SDL surface.
         * We retreive the last frame in the stream (av_seek_frame) in order
         * to minimize the latency.
         * The loop is exited uppon end of stream or if and av_* function
         * returned and error.
         * */
        start = SDL_GetTicks();
        for(i=i ; i<loops && !end ; ++i) {
            // For FPS
            loop_start = SDL_GetTicks();
    
            // Events
            while( SDL_PollEvent(&event) ) {
                switch(event.type) {
                    case SDL_QUIT:
                        end=1;
                        break;
                    case SDL_KEYDOWN:
                        switch(event.key.keysym.sym) {
                            case SDLK_ESCAPE:
                                end=1;
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
            }
    
            // Goto last received frame (min delay)
            // TODO: besoin que d'une fois ou a chaque tour ?
            /* NOTE: av_seek_frame(...) will return an error for each seek
             * (Invalid seek) however it seems that it seeks to the stream 
             * end anyway, so we just throw awaa those errors.
             * */
            av_seek_frame(avm.fmtCtx, -1, avm.fmtCtx->duration, 0);
            avcodec_flush_buffers(avm.codecCtx);
    
            // Get Frame
            if( av_read_frame(avm.fmtCtx, &packet)<0 ) 
                break;
            
    
            // Is frame from video stream
            if( packet.stream_index==avm.videoStreamIdx ) {
                if( avcodec_decode_video2(avm.codecCtx, frame, 
                            &finishedFrame, &packet)<0 )
                    break;

                // A video frame ?
                if( finishedFrame ) {
                    SDL_LockYUVOverlay(bmp);
                    pict.data[0] = bmp->pixels[0];
                    pict.data[1] = bmp->pixels[2];
                    pict.data[2] = bmp->pixels[1];
                    pict.linesize[0] = bmp->pitches[0];
                    pict.linesize[1] = bmp->pitches[2];
                    pict.linesize[2] = bmp->pitches[1];
    
                    av_picture_copy(&pict, (AVPicture*)frame, frame->format, 
                            avm.codecCtx->width, avm.codecCtx->height);
    
                    SDL_UnlockYUVOverlay(bmp);
                    
                    SDL_DisplayYUVOverlay(bmp, &rect);
                } else {
                    fprintf(stdout, " WTF\n");
                }
            }
            av_free_packet(&packet);
    
            fprintf(stdout, "\rframes=%d", i); 
            fflush(stdout);
    
            // Regulate FPS
            if(1000.0/frameRate > SDL_GetTicks()-start) {
                SDL_Delay(1000.0/frameRate - (SDL_GetTicks()-loop_start));
            }
        }
        fprintf(stdout, "\n"); // new line after "frames=xx"

        // Why did we exited from the for loop ?
        if( HAS_SIG(sigs_received, FLAG_SIGINT) ) { // SIGINT
            fprintf(stdout, "Got SIGINT, waiting for child process to exit ...\n");
            kill(rtmpdump_pid, SIGTERM);
            wait(&status);
            break;
        } else if( HAS_SIG(sigs_received, FLAG_SIGCHLD) ) { // SIGCHLD
            fprintf(stderr, "RTMPDUMP stoppped, trying restart ...\n");
            input_media_close();
            if( make_child_process()!=0 ){
                fprintf(stderr, "Failed to restart RTMPDUMP, EXITING.\n");
            }
            if( input_media_open()!=0 ) {
                fprintf(stderr, "Failed to open input media, EXITING.\n");
            }
        } else if( i>=loops ) { // read all frames
            break;
        } else if( end ) { // SDL quit
            break;
        } else { // Unknown
            fprintf(stderr, "Unhandled error, EXITING.\n");
            break;
        }
    
    } while(1);
    fprintf(stdout, "GOT %d frames in %d ms\n", i, (SDL_GetTicks()-start));

    input_media_close();
    av_free(frame);

    return 0;
}









