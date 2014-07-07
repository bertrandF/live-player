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

int
main(int argc, char** argv )
{
    char opt;
    int i, videoStreamIdx=-1;
    int pid, pipefd[2];
    uint32_t start;
    int end = 0;
    
    AVFormatContext *fmtCtx;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    AVPacket packet;
    AVFrame *frame;
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
        return -1;
    }
    
    // Dump configuration
    fprintf(stdout, "framerate=%d\n",   frameRate);
    fprintf(stdout, "loops=%d\n",       loops);
    fprintf(stdout, "sleeptime=%d\n",   sleepTime);
    fprintf(stdout, "URL=%s\n",         url);

    
    // --------------------- Start RTMP dump child process ----------
    if(pipe(pipefd) != 0) {
        fprintf(stderr, "Cannot create pipe.\n");
        return -1;
    }

    pid=fork();
    switch(pid) {
        case -1: // error
            fprintf(stderr, "Could not create child process for rtmpdump.\n");
            return -1;
        case 0: // child
            close(pipefd[0]); // read end unused
            close(STDOUT_FILENO);
            dup(pipefd[1]); // pipe write end is now stdout
            close(pipefd[1]);
            execl("/usr/bin/rtmpdump", "--quiet", "--live", "--realtime", "-r", url, (char*)NULL);
            break;
        default: // parent
            close(pipefd[1]); // write end unused
            close(STDIN_FILENO);
            dup(pipefd[0]); // pipe read end is now stdin
            close(pipefd[0]);
            break;
    }


    // --------------------- Configure lib AV ------------------------
    av_register_all();
    fmtCtx = avformat_alloc_context();
    if(!fmtCtx) {
        fprintf(stderr, "Cannot create AV context.\n");
        return -1;
    }
    
    // Open input file
    if( avformat_open_input(&fmtCtx, DEFAULT_INFILE, 0, NULL)!=0 ) {
        fprintf(stderr, "Cannot open input file.\n");
        return -1;
    }

    // Retreive stream information
    if( avformat_find_stream_info(fmtCtx, NULL)!=0 ) {
        fprintf(stderr, "Cannot find stream info.\n");
        return -1;
    }
    av_dump_format(fmtCtx, 0, DEFAULT_INFILE, 0);

    // Find the first video stream
    for(i=0 ; i<fmtCtx->nb_streams ; ++i) {
        if(fmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx=i;
            break;
        }
    }
    if( videoStreamIdx==-1 ) {
        fprintf(stderr, "Cannot find video stream.\n");
        return -1;
    }

    // Find the codec
    codecCtx = fmtCtx->streams[videoStreamIdx]->codec;
    codec = avcodec_find_decoder(codecCtx->codec_id);
    if( codec==NULL ) {
        fprintf(stderr, "Unsupported decoder.\n");
        return -1;
    }
    if( avcodec_open2(codecCtx, codec, NULL)<0 ) {
        fprintf(stderr, "Cannot open codec.\n");
        return -1;
    }

    // Allocate space for video frame
    frame = av_frame_alloc();
    if( !frame ) {
        fprintf(stderr, "Cannot allocate video frame.\n");
        return -1;
    }


    // --------------------- Configure SDL --------------------------
    if( SDL_Init(SDL_INIT_VIDEO)==-1 ) {
        fprintf(stderr, "Cannot init SDL.\n");
        return -1;
    }
    atexit(SDL_Quit);
    screen = SDL_SetVideoMode(codecCtx->width, codecCtx->height, 16, SDL_HWSURFACE);
    if(screen==NULL) {
        fprintf(stderr, "Cannot create SDL surface.\n");
        return -1;
    }

    // Allocate place to put YUV image to display
    bmp = SDL_CreateYUVOverlay(codecCtx->width, codecCtx->height, SDL_YV12_OVERLAY, screen);
    if( !bmp ) {
        fprintf(stderr, "Cannot create overlay.\n");
        return -1;
    }

    
    // --------------------- Process video frames -------------------
    rect.x = 0;
    rect.y = 0;
    rect.w = codecCtx->width;
    rect.h = codecCtx->height;
    for(i=0 ; i<loops && !end ; ++i) {
        // For FPS
        start = SDL_GetTicks();

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
        av_seek_frame(fmtCtx, -1, fmtCtx->duration, 0);
        avcodec_flush_buffers(codecCtx);

        // Get Frame
        if( av_read_frame(fmtCtx, &packet)<0 ) {
            fprintf(stderr, "Cannot read frame.\n");
            return -1;
        }

        // Is frame from video stream
        if( packet.stream_index==videoStreamIdx ) {
            avcodec_decode_video2(codecCtx, frame, &finishedFrame, &packet);
            // A video frame ?
            if( finishedFrame ) {
                SDL_LockYUVOverlay(bmp);
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];
                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];

                av_picture_copy(&pict, (AVPicture*)frame, frame->format, codecCtx->width,
                        codecCtx->height);

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
            SDL_Delay(1000.0/frameRate - (SDL_GetTicks()-start));
        }
    }
    fprintf(stdout, "GOT in %d secs, %d frames\n", (SDL_GetTicks()-start), loops);
    return 0;
}









