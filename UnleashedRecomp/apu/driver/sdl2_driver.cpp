#include <apu/audio.h>
#include <cpu/guest_thread.h>
#include <kernel/heap.h>
#include <os/logger.h>
#include <user/config.h>

#include <atomic>
#include <memory>

#if defined(__SWITCH__)
#include <pthread.h>
#endif

static PPCFunc* g_clientCallback{};
static uint32_t g_clientCallbackParam{}; // pointer in guest memory
static SDL_AudioDeviceID g_audioDevice{};
static bool g_audioDeviceReady{};
static bool g_downMixToStereo;

static void CreateAudioDevice()
{
    if (g_audioDevice != NULL)
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        g_audioDeviceReady = false;
    }

    bool surround = Config::ChannelConfiguration == EChannelConfiguration::Surround;
    int allowedChanges = surround ? SDL_AUDIO_ALLOW_CHANNELS_CHANGE : 0;

    SDL_AudioSpec desired{}, obtained{};
    desired.freq = XAUDIO_SAMPLES_HZ;
    desired.format = AUDIO_F32SYS;
    desired.channels = surround ? XAUDIO_NUM_CHANNELS : 2;
    desired.samples = XAUDIO_NUM_SAMPLES;
    g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, allowedChanges);

    if (g_audioDevice && obtained.channels != 2 && obtained.channels != XAUDIO_NUM_CHANNELS) // This check may fail only when surround sound is enabled.
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        obtained = {};
        g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    }

    if (!g_audioDevice)
    {
        LOGFN_ERROR("Failed to open audio device: {}", SDL_GetError());
        g_downMixToStereo = true;
        g_audioDeviceReady = false;
        return;
    }

    g_audioDeviceReady = true;
    g_downMixToStereo = (obtained.channels == 2);
}

void XAudioInitializeSystem()
{
#ifdef _WIN32
    // Force wasapi on Windows.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

#if defined(__SWITCH__)
    XAudioSetGuestCallbacksEnabled(false);
#endif

    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, "Unleashed Recompiled");

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        LOGFN_ERROR("Failed to init audio subsystem: {}", SDL_GetError());
        return;
    }

    CreateAudioDevice();
}

#if defined(__SWITCH__)
static pthread_t g_audioThread{};
static bool g_audioThreadCreated{};
#else
static std::unique_ptr<std::thread> g_audioThread;
#endif
static std::atomic<bool> g_audioThreadShouldExit;
static std::atomic<uint32_t> g_audioCallbackCount;
static std::atomic<uint32_t> g_audioSubmitCount;
#if defined(__SWITCH__)
static std::atomic<bool> g_audioGuestCallbacksEnabled;
#endif

void XAudioSetGuestCallbacksEnabled(bool enabled)
{
#if defined(__SWITCH__)
    g_audioGuestCallbacksEnabled.store(enabled, std::memory_order_release);
#else
    (void)enabled;
#endif
}

static bool AreGuestCallbacksEnabled()
{
#if defined(__SWITCH__)
    return g_audioGuestCallbacksEnabled.load(std::memory_order_acquire);
#else
    return true;
#endif
}

#if defined(__SWITCH__)
static void* AudioThread(void*)
#else

static void AudioThread()
#endif
{
    using namespace std::chrono_literals;

    std::unique_ptr<GuestThreadContext> ctx;

    size_t channels = g_downMixToStereo ? 2 : XAUDIO_NUM_CHANNELS;

    while (!g_audioThreadShouldExit.load(std::memory_order_acquire))
    {
        uint32_t queuedAudioSize = g_audioDevice ? SDL_GetQueuedAudioSize(g_audioDevice) : 0;
        constexpr size_t MAX_LATENCY = 10;
        const size_t callbackAudioSize = channels * XAUDIO_NUM_SAMPLES * sizeof(float);

        if ((queuedAudioSize / callbackAudioSize) <= MAX_LATENCY)
        {
            if (AreGuestCallbacksEnabled() && g_clientCallback != nullptr)
            {
                if (ctx == nullptr)
                    ctx = std::make_unique<GuestThreadContext>(0);

                ctx->ppcContext.r3.u32 = g_clientCallbackParam;
                g_clientCallback(ctx->ppcContext, g_memory.base);
            }
        }

        auto now = std::chrono::steady_clock::now();
        constexpr auto INTERVAL = 1000000000ns * XAUDIO_NUM_SAMPLES / XAUDIO_SAMPLES_HZ;
        auto next = now + (INTERVAL - now.time_since_epoch() % INTERVAL);

        std::this_thread::sleep_for(std::chrono::floor<std::chrono::milliseconds>(next - now));

        while (std::chrono::steady_clock::now() < next)
            std::this_thread::yield();
    }
}

static void CreateAudioThread()
{
    SDL_PauseAudioDevice(g_audioDevice, 0);
    g_audioThreadShouldExit = false;
#if defined(__SWITCH__)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    constexpr auto AUDIO_THREAD_STACK_SIZE = 2 * 1024 * 1024;
    const auto stackResult = pthread_attr_setstacksize(&attr, AUDIO_THREAD_STACK_SIZE);
    if (stackResult != 0)
        LOGFN_ERROR("Switch XAudio pthread_attr_setstacksize failed: 0x{:X}", stackResult);

    const auto createResult = pthread_create(&g_audioThread, &attr, AudioThread, nullptr);
    pthread_attr_destroy(&attr);
    if (createResult != 0)
    {
        LOGFN_ERROR("Switch XAudio pthread_create failed: 0x{:X}", createResult);
        return;
    }

    g_audioThreadCreated = true;
    LOGFN("Switch XAudio thread created: stackBytes={}, outputReady={}",
        AUDIO_THREAD_STACK_SIZE,
        g_audioDeviceReady);
#else
    g_audioThread = std::make_unique<std::thread>(AudioThread);
#endif
}

void XAudioRegisterClient(PPCFunc* callback, uint32_t param)
{
    auto* pClientParam = static_cast<uint32_t*>(g_userHeap.Alloc(sizeof(param)));
    ByteSwapInplace(param);
    *pClientParam = param;
    g_clientCallbackParam = g_memory.MapVirtual(pClientParam);
    g_clientCallback = callback;

    CreateAudioThread();
}

void XAudioSubmitFrame(void* samples)
{
    auto floatSamples = reinterpret_cast<be<float>*>(samples);

    if (g_downMixToStereo)
    {
        // 0: left 1.0f, right 0.0f
        // 1: left 0.0f, right 1.0f
        // 2: left 0.75f, right 0.75f
        // 3: left 0.0f, right 0.0f
        // 4: left 1.0f, right 0.0f
        // 5: left 0.0f, right 1.0f

        std::array<float, 2 * XAUDIO_NUM_SAMPLES> audioFrames;

        for (size_t i = 0; i < XAUDIO_NUM_SAMPLES; i++)
        {
            float ch0 = floatSamples[0 * XAUDIO_NUM_SAMPLES + i];
            float ch1 = floatSamples[1 * XAUDIO_NUM_SAMPLES + i];
            float ch2 = floatSamples[2 * XAUDIO_NUM_SAMPLES + i];
            float ch3 = floatSamples[3 * XAUDIO_NUM_SAMPLES + i];
            float ch4 = floatSamples[4 * XAUDIO_NUM_SAMPLES + i];
            float ch5 = floatSamples[5 * XAUDIO_NUM_SAMPLES + i];

            audioFrames[i * 2 + 0] = (ch0 + ch2 * 0.75f + ch4) * Config::MasterVolume;
            audioFrames[i * 2 + 1] = (ch1 + ch2 * 0.75f + ch5) * Config::MasterVolume;
        }

        SDL_QueueAudio(g_audioDevice, &audioFrames, sizeof(audioFrames));
    }
    else
    {
        std::array<float, XAUDIO_NUM_CHANNELS * XAUDIO_NUM_SAMPLES> audioFrames;

        for (size_t i = 0; i < XAUDIO_NUM_SAMPLES; i++)
        {
            for (size_t j = 0; j < XAUDIO_NUM_CHANNELS; j++)
                audioFrames[i * XAUDIO_NUM_CHANNELS + j] = floatSamples[j * XAUDIO_NUM_SAMPLES + i] * Config::MasterVolume;
        }

        SDL_QueueAudio(g_audioDevice, &audioFrames, sizeof(audioFrames));
    }
}
