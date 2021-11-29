
#include <SDL2/SDL.h>
#include <inttypes.h>
#include <stdio.h>

#define MUS_PATH "b5.wav"

// variable declarations
static Uint8 *audio_pos; // global pointer to the audio buffer to be played
static Uint32 audio_len; // remaining length of the sample we have to play
// prototype for our audio callback
// see the implementation for more information
void
my_audio_callback(void *userdata, Uint8 *stream, int len) {

    if (audio_len == 0)
        return;

    len = (len > audio_len ? audio_len : len);
    // SDL_memcpy (stream, audio_pos, len); 					// simply copy from one buffer into the
    // other
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME); // mix from one buffer into another

    audio_pos += len;
    audio_len -= len;
}

int
main(int argc, char *argv[]) {

    // Initialize SDL.
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
        return 1;

    // local variables
    static Uint32 wav_length;      // length of our sample
    static Uint8 *wav_buffer;      // buffer containing our audio file
    static SDL_AudioSpec wav_spec; // the specs of our piece of music

    /* Load the WAV */
    // the specs, length and buffer of our wav are filled
    if (SDL_LoadWAV(MUS_PATH, &wav_spec, &wav_buffer, &wav_length) == NULL) {
        return 1;
    }
    // set the callback function
    wav_spec.callback = NULL;
    wav_spec.userdata = NULL;
    // set our global static variables
    audio_pos = wav_buffer; // copy sound buffer
    audio_len = wav_length; // copy file length

    SDL_AudioDeviceID device_id = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE);
    printf("device id %d\n", device_id);
    /* Open the audio device */
    if (device_id < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(-1);
    }
    
    // 开始播放声音
    SDL_PauseAudioDevice(device_id, 0);
    while (1) {
        int ret = SDL_QueueAudio(device_id, wav_buffer, wav_length);
        if (ret < 0) {
            printf("queue audio failed, error: <%s>\n", SDL_GetError());
            exit(-1);
        }
        SDL_Delay(1000);
    }

    // shut everything down
    SDL_CloseAudio();
    SDL_FreeWAV(wav_buffer);
}