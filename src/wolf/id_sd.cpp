// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
//
//      ID Engine
//      ID_SD.c - Sound Manager for Wolfenstein 3D
//      v1.2
//      By Jason Blochowiak
//

//
//      This module handles dealing with generating sound on the appropriate
//              hardware
//
//      Depends on: User Mgr (for parm checking)
//
//      Globals:
//              For User Mgr:
//                      SoundBlasterPresent - SoundBlaster card present?
//                      AdLibPresent - AdLib card present?
//                      SoundMode - What device is used for sound effects
//                              (Use SM_SetSoundMode() to set)
//                      MusicMode - What device is used for music
//                              (Use SM_SetMusicMode() to set)
//                      DigiMode - What device is used for digitized sound effects
//                              (Use SM_SetDigiDevice() to set)
//
//              For Cache Mgr:
//                      NeedsDigitized - load digitized sounds?
//                      NeedsMusic - load music?
//

#include "wl_def.h"
#include "WolfNative.hpp"
#include "opl3.h"

#define ORIGSAMPLERATE 7042

static Uint16 mix_format;
static int mix_channels;

typedef struct
{
    uint32_t startpage;
    uint32_t length;
} digiinfo;

static WolfMix_Chunk SoundChunks[STARTMUSIC - STARTDIGISOUNDS] = {{0}};

// [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
Wolf_mutex *audioMutex;
struct NativeAudioLock {
    NativeAudioLock(){Wolf_LockMutex(audioMutex);}
    ~NativeAudioLock(){Wolf_UnlockMutex(audioMutex);}
};

globalsoundpos channelSoundPos[MIX_CHANNELS];

//      Global variables
        boolean         AdLibPresent,
                        SoundBlasterPresent,SBProPresent,
                        SoundPositioned;
        SDMode          SoundMode;
        SMMode          MusicMode;
        SDSMode         DigiMode;
static  byte          **SoundTable;
        int             DigiMap[LASTSOUND];
        int             DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

//      Internal variables
static  boolean                 SD_Started;
static  boolean                 nextsoundpos;
static  soundnames              SoundNumber;
static  soundnames              DigiNumber;
static  word                    SoundPriority;
static  word                    DigiPriority;
static  int                     LeftPosition;
static  int                     RightPosition;

        word                    NumDigi;
static  digiinfo               *DigiList;
static  boolean                 DigiPlaying;

//      PC Sound variables
static  volatile byte           pcLastSample;
static  byte * volatile         pcSound;
static  longword                pcLengthLeft;

//      AdLib variables
static  byte * volatile         alSound;
static  byte                    alBlock;
static  longword                alLengthLeft;
static  longword                alTimeCount;
static  Instrument              alZeroInst;

//      Sequencer variables
static  volatile boolean        sqActive;
static  word                   *sqHack;
static  word                   *sqHackPtr;
static  int                     sqHackLen;
static  int                     sqHackSeqLen;
static  longword                sqHackTime;

opl3_chip oplChip;

#define alOut(n,b) OPL3_WriteRegBuffered(&oplChip, n, b)

static void Wolf_SoundFinished(void)
{
	SoundNumber   = (soundnames)0;
	SoundPriority = 0;
}


///////////////////////////////////////////////////////////////////////////
//
//      Wolf_PCPlaySound() - Plays the specified sound on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_PCPlaySound(PCSound *sound)
{
    NativeAudioLock lock;
        pcLastSample = -1;
        pcLengthLeft = sound->common.length;
        pcSound = sound->data;
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_PCStopSound() - Stops the current sound playing on the PC Speaker
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_PCStopSound(void)
{
    NativeAudioLock lock;
        pcSound = 0;
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_ShutPC() - Turns off the pc speaker
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_ShutPC(void)
{
    NativeAudioLock lock;
        pcSound = 0;
}

// Adapted from Chocolate Doom (chocolate-doom/pcsound/pcsound_sdl.c)
#define SQUARE_WAVE_AMP 0x2000

static void Wolf_PCMixCallback(void *udata, Uint8 *stream, int len)
{
    NativeAudioLock lock;
    static int current_remaining = 0;
    static int current_freq = 0;
    static int phase_offset = 0;

    void *streamp = stream;
    Sint16 *leftptr;
    Sint16 *rightptr;
    Sint16 this_value;
    int oldfreq;
    int i;
    int nsamples;

    // Number of samples is quadrupled, because of 16-bit and stereo

    nsamples = len / 4;

    leftptr = (Sint16 *) streamp;
    rightptr = ((Sint16 *) streamp) + 1;

    // Fill the output buffer

    for (i=0; i<nsamples; ++i)
    {
        // Has this sound expired? If so, retrieve the next frequency

        while (current_remaining == 0)
        {
            oldfreq = current_freq;

            // Get the next frequency to play

            if(pcSound)
            {
                if(*pcSound!=pcLastSample)
                {
                    pcLastSample=*pcSound;

                    if(pcLastSample)
                        // The PC PIC counts down at 1.193180MHz
                        // So pwm_freq = counter_freq / reload_value
                        // reload_value = pcLastSample * 60 (see Wolf_DoFX)
                        current_freq = 1193180 / (pcLastSample * 60);
                    else
                        current_freq = 0;

                    // The PC speaker sample rate is 140Hz (see Wolf_t0SlowAsmService)
                    current_remaining = param_samplerate / 140;
                }
                pcSound++;
                pcLengthLeft--;
                if(!pcLengthLeft)
                {
                    pcSound=0;
                    SoundNumber=(soundnames)0;
                    SoundPriority=0;
                }
            }
            else
            {
                current_freq = 0;
                current_remaining = 1;
            }

            if (current_freq != 0)
            {
                // Adjust phase to match to the new frequency.
                // This gives us a smooth transition between different tones,
                // with no impulse changes.

                phase_offset = (phase_offset * oldfreq) / current_freq;
            }
        }

        // Set the value for this sample.

        if (current_freq == 0)
        {
            // Silence

            this_value = 0;
        }
        else
        {
            int frac;

            // Determine whether we are at a peak or trough in the current
            // sound.  Multiply by 2 so that frac % 2 will give 0 or 1
            // depending on whether we are at a peak or trough.

            frac = (phase_offset * current_freq * 2) / param_samplerate;

            if ((frac % 2) == 0)
            {
                this_value = SQUARE_WAVE_AMP;
            }
            else
            {
                this_value = -SQUARE_WAVE_AMP;
            }

            ++phase_offset;
        }

        --current_remaining;

        // Use the same value for the left and right channels.

        *leftptr += this_value;
        *rightptr += this_value;

        leftptr += 2;
        rightptr += 2;
    }
}

void
SD_StopDigitized(void)
{
    DigiPlaying = false;
    DigiNumber = (soundnames) 0;
    DigiPriority = 0;
    SoundPositioned = false;
    if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        Wolf_SoundFinished();

    switch (DigiMode)
    {
        case sds_PC:
            Wolf_PCStopSound();
            break;
        case sds_SoundBlaster:
            WolfMix_HaltChannel(-1);
            break;
        default:
            break;
    }
}

int SD_GetChannelForDigi(int which)
{
    if(DigiChannel[which] != -1) return DigiChannel[which];

    int channel = WolfMix_GroupAvailable(1);
    if(channel == -1) channel = WolfMix_GroupOldest(1);
    if(channel == -1)           // All sounds stopped in the meantime?
        return WolfMix_GroupAvailable(1);
    return channel;
}

void SD_SetPosition(int channel, int leftpos, int rightpos)
{
    if((leftpos < 0) || (leftpos > 15) || (rightpos < 0) || (rightpos > 15)
            || ((leftpos == 15) && (rightpos == 15)))
        Quit("SD_SetPosition: Illegal position");

    switch (DigiMode)
    {
        case sds_SoundBlaster:
            WolfMix_SetPanning(channel, ((15 - leftpos) << 4) + 15,
                ((15 - rightpos) << 4) + 15);
            break;
        default:
            break;
    }
}

void SD_PrepareSound(int which)
{
    Wolf_AudioCVT cvt;

    if(DigiList == NULL)
        Quit("SD_PrepareSound(%i): DigiList not initialized!\n", which);

    int page = DigiList[which].startpage;
    int size = DigiList[which].length;

    byte *origsamples = PM_GetSound(page);
    if(origsamples + size >= PM_GetEnd())
        Quit("SD_PrepareSound(%i): Sound reaches out of page file!\n", which);

    if (Wolf_BuildAudioCVT(&cvt,
                          AUDIO_U8, 1, ORIGSAMPLERATE,
                          mix_format, mix_channels, param_samplerate) < 0)
    {
      Quit("Wolf_BuildAudioCVT: %s\n", Wolf_GetError());
    }

    cvt.len = size;
    cvt.buf = (Uint8 *)calloc(size_t(cvt.len), size_t(cvt.len_mult));
    if (!cvt.buf)
        Quit("SD_PrepareSound(%i): Unable to allocate audio buffer!\n", which);
    // [FG] clear buffer (cvt.len * cvt.len_mult >= cvt.len_cvt)
    memcpy(cvt.buf, origsamples, cvt.len);

    if (Wolf_ConvertAudio(&cvt) < 0)
    {
      free(cvt.buf);
      Quit("Wolf_ConvertAudio: %s\n", Wolf_GetError());
    }

    WolfMix_Chunk *const chunk = &SoundChunks[which];
    chunk->allocated = 1;
    chunk->volume = MIX_MAX_VOLUME;
    chunk->abuf = cvt.buf;
    chunk->alen = cvt.len_cvt;
}

// Returns the mixer channel, or -1 when nothing started. Channel 0 is a valid
// voice, so failure must not be reported as 0.
int SD_PlayDigitized(word which,int leftpos,int rightpos)
{
    if (!DigiMode)
        return -1;

    if (which >= NumDigi)
        Quit("SD_PlayDigitized: bad sound number %i", which);

    int channel = SD_GetChannelForDigi(which);
    SD_SetPosition(channel, leftpos,rightpos);

    DigiPlaying = true;

    WolfMix_Chunk *sample = &SoundChunks[which];
    if (sample->abuf == NULL)
    {
        printf("SoundChunks[%i] is NULL!\n", which);
        return -1;
    }

    if(WolfMix_PlayChannel(channel, sample, 0) == -1)
    {
        printf("Unable to play sound: %s\n", WolfMix_GetError());
        return -1;
    }

    return channel;
}

void SD_ChannelFinished(int channel)
{
    channelSoundPos[channel].valid = 0;
}

void
SD_SetDigiDevice(SDSMode mode)
{
    boolean devicenotpresent;

    if (mode == DigiMode)
        return;

    SD_StopDigitized();

    devicenotpresent = false;
    switch (mode)
    {
        case sds_SoundBlaster:
            if (!SoundBlasterPresent)
                devicenotpresent = true;
            break;
        default:
            break;
    }

    if (!devicenotpresent)
    {
        DigiMode = mode;

#ifdef NOTYET
        Wolf_SetTimerSpeed();
#endif
    }
}

void
Wolf_SetupDigi(void)
{
    // Correct padding enforced by PM_Startup()
    word *soundInfoPage = (word *) (void *) PM_GetPage(ChunksInFile-1);
    NumDigi = (word) PM_GetPageSize(ChunksInFile - 1) / 4;

    DigiList = (digiinfo *) malloc(NumDigi * sizeof(digiinfo));
    int i;
    for(i = 0; i < NumDigi; i++)
    {
        // Calculate the size of the digi from the sizes of the pages between
        // the start page and the start page of the next sound

        DigiList[i].startpage = soundInfoPage[i * 2];
        if((int) DigiList[i].startpage >= ChunksInFile - 1)
        {
            NumDigi = i;
            break;
        }

        int lastPage;
        if(i < NumDigi - 1)
        {
            lastPage = soundInfoPage[i * 2 + 2];
            if(lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1) lastPage = ChunksInFile - 1;
            else lastPage += PMSoundStart;
        }
        else lastPage = ChunksInFile - 1;

        int size = 0;
        for(int page = PMSoundStart + DigiList[i].startpage; page < lastPage; page++)
            size += PM_GetPageSize(page);

        // Don't include padding of sound info page, if padding was added
        if(lastPage == ChunksInFile - 1 && PMSoundInfoPagePadded) size--;

        // Patch lower 16-bit of size with size from sound info page.
        // The original VSWAP contains padding which is included in the page size,
        // but not included in the 16-bit size. So we use the more precise value.
        if((size & 0xffff0000) != 0 && (size & 0xffff) < soundInfoPage[i * 2 + 1])
            size -= 0x10000;
        size = (size & 0xffff0000) | soundInfoPage[i * 2 + 1];

        DigiList[i].length = size;
    }

    for(i = 0; i < LASTSOUND; i++)
    {
        DigiMap[i] = -1;
        DigiChannel[i] = -1;
    }
}

//      AdLib Code

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_ALStopSound() - Turns off any sound effects playing through the
//              AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_ALStopSound(void)
{
    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    Wolf_LockMutex(audioMutex);

    alSound = 0;
    alOut(alFreqH + 0, 0);

    Wolf_UnlockMutex(audioMutex);
}

static void
Wolf_AlSetFXInst(Instrument *inst)
{
    byte c,m;

    m = 0;      // modulator cell for channel 0
    c = 3;      // carrier cell for channel 0
    alOut(m + alChar,inst->mChar);
    alOut(m + alScale,inst->mScale);
    alOut(m + alAttack,inst->mAttack);
    alOut(m + alSus,inst->mSus);
    alOut(m + alWave,inst->mWave);
    alOut(c + alChar,inst->cChar);
    alOut(c + alScale,inst->cScale);
    alOut(c + alAttack,inst->cAttack);
    alOut(c + alSus,inst->cSus);
    alOut(c + alWave,inst->cWave);

    // Note: Switch commenting on these lines for old MUSE compatibility
//    alOutInIRQ(alFeedCon,inst->nConn);
    alOut(alFeedCon,0);
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_ALPlaySound() - Plays the specified sound on the AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_ALPlaySound(AdLibSound *sound)
{
    Instrument      *inst;
    byte            *data;

    Wolf_ALStopSound();

    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    Wolf_LockMutex(audioMutex);

    alLengthLeft = sound->common.length;
    data = sound->data;
    alBlock = ((sound->block & 7) << 2) | 0x20;
    inst = &sound->inst;

    if (!(inst->mSus | inst->cSus))
    {
        Quit("Wolf_ALPlaySound() - Bad instrument");
    }

    Wolf_AlSetFXInst(inst);
    alSound = (byte *)data;

    Wolf_UnlockMutex(audioMutex);
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_ShutAL() - Shuts down the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_ShutAL(void)
{
    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    Wolf_LockMutex(audioMutex);

    alSound = 0;
    alOut(alEffects,0);
    alOut(alFreqH + 0,0);
    Wolf_AlSetFXInst(&alZeroInst);

    Wolf_UnlockMutex(audioMutex);
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_StartAL() - Starts up the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_StartAL(void)
{
    alOut(alEffects, 0);
    Wolf_AlSetFXInst(&alZeroInst);
}

////////////////////////////////////////////////////////////////////////////
//
//      Wolf_ShutDevice() - turns off whatever device was being used for sound fx
//
////////////////////////////////////////////////////////////////////////////
static void
Wolf_ShutDevice(void)
{
    switch (SoundMode)
    {
        case sdm_PC:
            Wolf_ShutPC();
            break;
        case sdm_AdLib:
            Wolf_ShutAL();
            break;
        default:
            break;
    }
    SoundMode = sdm_Off;
}

///////////////////////////////////////////////////////////////////////////
//
//      Wolf_StartDevice() - turns on whatever device is to be used for sound fx
//
///////////////////////////////////////////////////////////////////////////
static void
Wolf_StartDevice(void)
{
    switch (SoundMode)
    {
        case sdm_AdLib:
            Wolf_StartAL();
            break;
        default:
            break;
    }
    SoundNumber = (soundnames) 0;
    SoundPriority = 0;
}

//      Public routines

///////////////////////////////////////////////////////////////////////////
//
//      SD_SetSoundMode() - Sets which sound hardware to use for sound effects
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetSoundMode(SDMode mode)
{
    boolean result = false;
    word    tableoffset;

    SD_StopSound();

    if ((mode == sdm_AdLib) && !AdLibPresent)
        mode = sdm_PC;

    switch (mode)
    {
        case sdm_Off:
            tableoffset = STARTADLIBSOUNDS;
            result = true;
            break;
        case sdm_PC:
            tableoffset = STARTPCSOUNDS;
            result = true;
            break;
        case sdm_AdLib:
            tableoffset = STARTADLIBSOUNDS;
            if (AdLibPresent)
                result = true;
            break;
        default:
            Quit("SD_SetSoundMode: Invalid sound mode %i", mode);
            return false;
    }
    SoundTable = &audiosegs[tableoffset];

    if (result && (mode != SoundMode))
    {
        Wolf_ShutDevice();
        SoundMode = mode;
        Wolf_StartDevice();
    }

    return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_SetMusicMode() - sets the device to use for background music
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetMusicMode(SMMode mode)
{
    boolean result = false;

    SD_FadeOutMusic();
    while (SD_MusicPlaying())
        Wolf_Delay(5);

    switch (mode)
    {
        case smm_Off:
            result = true;
            break;
        case smm_AdLib:
            if (AdLibPresent)
                result = true;
            break;
    }

    if (result)
        MusicMode = mode;

    return(result);
}

int numreadysamples = 0;
byte *curAlSound = 0;
byte *curAlSoundPtr = 0;
longword curAlLengthLeft = 0;
int soundTimeCounter = 5;
int samplesPerMusicTick;

void Wolf_IMFMusicPlayer(void *udata, Uint8 *stream, int len)
{
    int stereolen = len>>1;
    int sampleslen = stereolen>>1;
    int16_t *stream16 = (int16_t *) (void *) stream;    // expect correct alignment

    while(1)
    {
        if(numreadysamples)
        {
            if(numreadysamples<sampleslen)
            {
                OPL3_GenerateStream(&oplChip, stream16, numreadysamples);
                stream16 += numreadysamples*2;
                sampleslen -= numreadysamples;
            }
            else
            {
                OPL3_GenerateStream(&oplChip, stream16, sampleslen);
                numreadysamples -= sampleslen;
                return;
            }
        }

        // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
        Wolf_LockMutex(audioMutex);

        soundTimeCounter--;
        if(!soundTimeCounter)
        {
            soundTimeCounter = 5;

            // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
			// [k1n9_duk3] THIS is the way the original Wolfenstein 3-D code handled it!
			if(alSound)
			{
				if(*alSound)
				{
					alOut(alFreqL, *alSound);
					alOut(alFreqH, alBlock);
				} else alOut(alFreqH, 0);
				alSound++;
				if (!(--alLengthLeft))
				{
					alSound = 0;
					SoundPriority=0;
					alOut(alFreqH, 0);
				}
			}
        }
        if(sqActive)
        {
            do
            {
                if(sqHackTime > alTimeCount) break;
                sqHackTime = alTimeCount + *(sqHackPtr+1);
                alOut(*(byte *) sqHackPtr, *(((byte *) sqHackPtr)+1));
                sqHackPtr += 2;
                sqHackLen -= 4;
            }
            while(sqHackLen>0);
            alTimeCount++;
            if(!sqHackLen)
            {
                sqHackPtr = sqHack;
                sqHackLen = sqHackSeqLen;
                sqHackTime = 0;
                alTimeCount = 0;
            }
        }
        numreadysamples = samplesPerMusicTick;

        Wolf_UnlockMutex(audioMutex);
    }
}

static int GetSliceSize(void)
{
  int limit, n;

  if (param_audiobuffer != -1)
    return param_audiobuffer;

  // Multiply first: 48/96 kHz otherwise divide by a truncated zero divisor.
  limit = int((int64_t(2048) * param_samplerate) / 44100);

  // Try all powers of two, not exceeding the limit.

  for (n = 0; ; n++)
  {
    // 2^n <= limit < 2^n+1 ?

    if ((1 << (n + 1)) > limit)
    {
      return (1 << n);
    }
  }

  // Should never happen?

  return 1024;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_Startup() - starts up the Sound Mgr
//              Detects all additional sound hardware and installs my ISR
//
///////////////////////////////////////////////////////////////////////////
void
SD_Startup(void)
{
    if (SD_Started)
        return;

    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    if (!(audioMutex = Wolf_CreateMutex()))
    {
        puts("Unable to create audio mutex");
        return;
    }

    if (WolfMix_OpenAudioDevice(param_samplerate, AUDIO_S16SYS, 2, GetSliceSize(), NULL,
                            Wolf_AUDIO_ALLOW_FREQUENCY_CHANGE) < 0)
    {
        printf("Unable to open audio: %s\n", WolfMix_GetError());
        Wolf_DestroyMutex(audioMutex);
        audioMutex = NULL;
        return;
    }

    WolfMix_QuerySpec(&param_samplerate, &mix_format, &mix_channels);
    WolfMix_ReserveChannels(2);  // reserve player and boss weapon channels
    WolfMix_GroupChannels(2, MIX_CHANNELS-1, 1); // group remaining channels

    // Init music

    samplesPerMusicTick = param_samplerate / 700;    // Wolf_t0FastAsmService played at 700Hz

    OPL3_Reset(&oplChip, param_samplerate);

    WolfMix_HookMusic(Wolf_IMFMusicPlayer, 0);
    WolfMix_ChannelFinished(SD_ChannelFinished);
    AdLibPresent = true;
    SoundBlasterPresent = true;

    alTimeCount = 0;

    // Add PC speaker sound mixer
    WolfMix_SetPostMix(Wolf_PCMixCallback, NULL);

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    Wolf_SetupDigi();

    SD_Started = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_Shutdown() - shuts down the Sound Mgr
//              Removes sound ISR and turns off whatever sound hardware was active
//
///////////////////////////////////////////////////////////////////////////
void
SD_Shutdown(void)
{
    if (!SD_Started)
        return;

    Wolf_NativeAudioShutdown();
    SD_MusicOff();
    SD_StopSound();

    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    if (audioMutex)
    {
        Wolf_DestroyMutex(audioMutex);
        audioMutex = NULL;
    }

    for(int i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
    {
        if (SoundChunks[i].abuf) free(SoundChunks[i].abuf);
        memset(&SoundChunks[i], 0, sizeof(SoundChunks[i]));
    }

    free(DigiList);

    SD_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_PositionSound() - Sets up a stereo imaging location for the next
//              sound to be played. Each channel ranges from 0 to 15.
//
///////////////////////////////////////////////////////////////////////////
void
SD_PositionSound(int leftvol,int rightvol)
{
    LeftPosition = leftvol;
    RightPosition = rightvol;
    nextsoundpos = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_PlaySound() - plays the specified sound on the appropriate hardware
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_PlaySound(soundnames sound)
{
    boolean         ispos;
    SoundCommon     *s;
    int             lp,rp;

    lp = LeftPosition;
    rp = RightPosition;
    LeftPosition = 0;
    RightPosition = 0;

    ispos = nextsoundpos;
    nextsoundpos = false;

    if (sound == (soundnames)-1 || (DigiMode == sds_Off && SoundMode == sdm_Off))
        return 0;
    if (Wolf_MenuCue (sound))
        return 0;

    void *p = SoundTable[sound];
    s = (SoundCommon *) p;

    if ((SoundMode != sdm_Off) && !s)
            Quit("SD_PlaySound() - Uncached sound");

    if ((DigiMode != sds_Off) && (DigiMap[sound] != -1))
    {
        if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        {
            if (s->priority < SoundPriority)
                return 0;

            Wolf_PCStopSound();

            SD_PlayDigitized(DigiMap[sound],lp,rp);
            SoundPositioned = ispos;
            SoundNumber = sound;
            SoundPriority = s->priority;
        }
        else
        {
#ifdef NOTYET
            if (s->priority < DigiPriority)
                return(false);
#endif

            int channel = SD_PlayDigitized(DigiMap[sound], lp, rp);
            SoundPositioned = ispos;
            DigiNumber = sound;
            DigiPriority = s->priority;
            return channel < 0 ? 0 : channel + 1;
        }

        return(true);
    }

    if (SoundMode == sdm_Off)
        return 0;

    if (!s->length)
        Quit("SD_PlaySound() - Zero length sound");
    if (s->priority < SoundPriority)
        return 0;

    switch (SoundMode)
    {
        case sdm_PC:
            Wolf_PCPlaySound((PCSound *)s);
            break;
        case sdm_AdLib:
            Wolf_ALPlaySound((AdLibSound *)s);
            break;
        default:
            break;
    }

    SoundNumber = sound;
    SoundPriority = s->priority;

    return 0;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_SoundPlaying() - returns the sound number that's playing, or 0 if
//              no sound is playing
//
///////////////////////////////////////////////////////////////////////////
word
SD_SoundPlaying(void)
{
    boolean result = false;

    switch (SoundMode)
    {
        case sdm_PC:
            result = pcSound? true : false;
            break;
        case sdm_AdLib:
            result = alSound? true : false;
            break;
        default:
            break;
    }

    if (result)
        return(SoundNumber);
    else
        return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_StopSound() - if a sound is playing, stops it
//
///////////////////////////////////////////////////////////////////////////
void
SD_StopSound(void)
{
    if (DigiPlaying)
        SD_StopDigitized();

    switch (SoundMode)
    {
        case sdm_PC:
            Wolf_PCStopSound();
            break;
        case sdm_AdLib:
            Wolf_ALStopSound();
            break;
        default:
            break;
    }

    SoundPositioned = false;

    Wolf_SoundFinished();
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_WaitSoundDone() - waits until the current sound is done playing
//
///////////////////////////////////////////////////////////////////////////
void
SD_WaitSoundDone(void)
{
    while (SD_SoundPlaying())
        Wolf_Delay(5);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicOn() - turns on the sequencer
//
///////////////////////////////////////////////////////////////////////////
void
SD_MusicOn(void)
{
    sqActive = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicOff() - turns off the sequencer and any playing notes
//      returns the last music offset for music continue
//
///////////////////////////////////////////////////////////////////////////
int
SD_MusicOff(void)
{
    word    i;

    // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
    Wolf_LockMutex(audioMutex);

    sqActive = false;
    switch (MusicMode)
    {
        case smm_AdLib:
            alOut(alEffects, 0);
            for (i = 0;i < sqMaxTracks;i++)
                alOut(alFreqH + i + 1, 0);
            break;
        default:
            break;
    }

    Wolf_UnlockMutex(audioMutex);

    return (int) (sqHackPtr-sqHack);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_StartMusic() - starts playing the music pointed to
//
///////////////////////////////////////////////////////////////////////////
void
SD_StartMusic(int chunk)
{
    SD_MusicOff();

    if (MusicMode == smm_AdLib)
    {
        // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
        Wolf_LockMutex(audioMutex);

        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        sqHack = (word *)(void *) audiosegs[chunk];     // alignment is correct
        if(*sqHack == 0) sqHackLen = sqHackSeqLen = chunkLen;
        else sqHackLen = sqHackSeqLen = *sqHack++;
        sqHackPtr = sqHack;
        sqHackTime = 0;
        alTimeCount = 0;
        SD_MusicOn();

        Wolf_UnlockMutex(audioMutex);
    }
}

void
SD_ContinueMusic(int chunk, int startoffs)
{
    SD_MusicOff();

    if (MusicMode == smm_AdLib)
    {
        // [DenisBelmondo] backport ecwolf/k1n9_duk3 fixes
        Wolf_LockMutex(audioMutex);

        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        sqHack = (word *)(void *) audiosegs[chunk];     // alignment is correct
        if(*sqHack == 0) sqHackLen = sqHackSeqLen = chunkLen;
        else sqHackLen = sqHackSeqLen = *sqHack++;
        sqHackPtr = sqHack;

        if(startoffs >= sqHackLen)
        {
            Quit("SD_StartMusic: Illegal startoffs provided!");
        }

        // fast forward to correct position
        // (needed to reconstruct the instruments)

        for(int i = 0; i < startoffs; i += 2)
        {
            byte reg = *(byte *)sqHackPtr;
            byte val = *(((byte *)sqHackPtr) + 1);
            if(reg >= 0xb1 && reg <= 0xb8) val &= 0xdf;           // disable play note flag
            else if(reg == 0xbd) val &= 0xe0;                     // disable drum flags

            alOut(reg,val);
            sqHackPtr += 2;
            sqHackLen -= 4;
        }
        sqHackTime = 0;
        alTimeCount = 0;

        SD_MusicOn();

        Wolf_UnlockMutex(audioMutex);
    }
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_FadeOutMusic() - starts fading out the music. Call SD_MusicPlaying()
//              to see if the fadeout is complete
//
///////////////////////////////////////////////////////////////////////////
void
SD_FadeOutMusic(void)
{
    switch (MusicMode)
    {
        case smm_AdLib:
            // DEBUG - quick hack to turn the music off
            SD_MusicOff();
            break;
        default:
            break;
    }
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicPlaying() - returns true if music is currently playing, false if
//              not
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_MusicPlaying(void)
{
    boolean result;

    switch (MusicMode)
    {
        case smm_AdLib:
            result = sqActive;
            break;
        default:
            result = false;
            break;
    }

    return(result);
}
