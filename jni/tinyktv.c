/* tinycap.c
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <tinyalsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

int capturing = 1;

unsigned int capture_sample(unsigned int card, unsigned int device,
                            unsigned int channels, unsigned int rate,
                            enum pcm_format format, unsigned int period_size,
                            unsigned int period_count, unsigned int cardWrite, unsigned int deviceWrite);

void sigint_handler(int sig __unused)
{
    capturing = 0;
}

int main(int argc, char **argv)
{
    struct wav_header header;
    unsigned int card = 0;
    unsigned int device = 0;
    unsigned int playCard = 0;
    unsigned int playDevice = 0;
    unsigned int channels = 2;
    unsigned int rate = 44100;
    unsigned int bits = 16;
    unsigned int frames;
    unsigned int period_size = 1024;
    unsigned int period_count = 4;
    enum pcm_format format;

    if (argc < 5) {
        fprintf(stderr, "Usage: %s [-D capture_card] [-d capture_device] -D2 play_card -d2 play_device [-c channels] "
                "[-r rate] [-b bits] [-p period_size] [-n n_periods]\n", argv[0]);
        return 1;
    }

    /* parse command line arguments */
    argv += 2;
    while (*argv) {
        if (strcmp(*argv, "-d") == 0) {
            argv++;
            if (*argv)
                device = atoi(*argv);

        } else if (strcmp(*argv, "-D2") == 0) {
            argv++;
            if (*argv)
                playCard = atoi(*argv);
        } else if (strcmp(*argv, "-d2") == 0) {
            argv++;
            if (*argv)
                playDevice = atoi(*argv);

        } else if (strcmp(*argv, "-c") == 0) {
            argv++;
            if (*argv)
                channels = atoi(*argv);
        } else if (strcmp(*argv, "-r") == 0) {
            argv++;
            if (*argv)
                rate = atoi(*argv);
        } else if (strcmp(*argv, "-b") == 0) {
            argv++;
            if (*argv)
                bits = atoi(*argv);
        } else if (strcmp(*argv, "-D") == 0) {
            argv++;
            if (*argv)
                card = atoi(*argv);
        } else if (strcmp(*argv, "-p") == 0) {
            argv++;
            if (*argv)
                period_size = atoi(*argv);
        } else if (strcmp(*argv, "-n") == 0) {
            argv++;
            if (*argv)
                period_count = atoi(*argv);
        }
        if (*argv)
            argv++;
    }

    header.riff_id = ID_RIFF;
    header.riff_sz = 0;
    header.riff_fmt = ID_WAVE;
    header.fmt_id = ID_FMT;
    header.fmt_sz = 16;
    header.audio_format = FORMAT_PCM;
    header.num_channels = channels;
    header.sample_rate = rate;

    switch (bits) {
    case 32:
        format = PCM_FORMAT_S32_LE;
        break;
    case 24:
        format = PCM_FORMAT_S24_LE;
        break;
    case 16:
        format = PCM_FORMAT_S16_LE;
        break;
    default:
        fprintf(stderr, "%d bits is not supported.\n", bits);
        return 1;
    }

    header.bits_per_sample = pcm_format_to_bits(format);
    header.byte_rate = (header.bits_per_sample / 8) * channels * rate;
    header.block_align = channels * (header.bits_per_sample / 8);
    header.data_id = ID_DATA;


    /* install signal handler and begin capturing */
    signal(SIGINT, sigint_handler);
    frames = capture_sample(card, device, header.num_channels,
                            header.sample_rate, format,
                            period_size, period_count, playCard, playDevice);
    fprintf(stderr, "Captured %d frames\n", frames);

    /* write header now all information is known */
    header.data_sz = frames * header.block_align;
    header.riff_sz = header.data_sz + sizeof(header) - 8;

    return 0;
}

unsigned int capture_sample(unsigned int card, unsigned int device,
                            unsigned int channels, unsigned int rate,
                            enum pcm_format format, unsigned int period_size,
                            unsigned int period_count, unsigned int cardWrite, unsigned int deviceWrite)
{
    struct pcm_config config;
    struct pcm *pcm;
    char *buffer;
    unsigned int size;
    unsigned int bytes_read = 0;

    memset(&config, 0, sizeof(config));
    config.channels = channels;
    config.rate = rate;
    config.period_size = period_size;
    config.period_count = period_count;
    config.format = format;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;


//    {
        struct pcm_config configWrite;
        struct pcm *pcmWrite;
        char *bufferWrite;
        int sizeWrite;
        int num_readWrite;

        memset(&configWrite, 0, sizeof(configWrite));
        configWrite.channels = channels;
        configWrite.rate = rate;
        configWrite.period_size = period_size;
        configWrite.period_count = period_count;
        configWrite.format = PCM_FORMAT_S16_LE;
        configWrite.start_threshold = 0;
        configWrite.stop_threshold = 0;
        configWrite.silence_threshold = 0;

        pcmWrite = pcm_open(cardWrite, deviceWrite, PCM_OUT, &config);
        if (!pcmWrite || !pcm_is_ready(pcmWrite)) {
            fprintf(stderr, "Unable to open PCM device (%s)\n",
                pcm_get_error(pcm));
            return 0;
        }

        fprintf(stderr, "Playback sample: %u ch, %u hz, %u bit\n", channels, rate,
               pcm_format_to_bits(format));
//    }

    pcm = pcm_open(card, device, PCM_IN, &config);
    if (!pcm || !pcm_is_ready(pcm)) {
        fprintf(stderr, "Unable to open PCM device (%s)\n",
                pcm_get_error(pcm));
        return 0;
    }

    size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
    size = 128;
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "Unable to allocate %d bytes\n", size);
        free(buffer);
        pcm_close(pcm);
        return 0;
    }

    fprintf(stderr, "Capturing sample: %u ch, %u hz, %u bit\n", channels, rate,
           pcm_format_to_bits(format));

    int w_index = 0;
    while (capturing && !pcm_read(pcm, buffer, size)) {
        if (pcm_write(pcmWrite, buffer, size)) {
            fprintf(stderr,"Error capturing sample\n");
            break;
        }

        if (w_index++ % 120 == 0)
        {
            fprintf(stderr,"\nplaying(size/package %d)", size);
        }
        fprintf(stderr,".");

        bytes_read += size;
    }

    free(buffer);
    pcm_close(pcm);
    return pcm_bytes_to_frames(pcm, bytes_read);
}

