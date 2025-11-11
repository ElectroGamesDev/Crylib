#include "Audio.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace cl
{
    // Internal audio system state
    struct AudioSystem
    {
        ma_engine engine;
        ma_context context;
        ma_resource_manager resourceManager;

        bool initialized = false;
        float masterVolume = 1.0f;
        AudioListener listener;

        // Recording
        ma_device recordingDevice;
        std::vector<float> recordingBuffer;
        bool isRecording = false;

        // Active sounds tracking - using unique IDs
        std::unordered_map<size_t, std::shared_ptr<ma_sound>> activeSounds;
        size_t nextSoundId = 0;

        // Effect processors
        std::unordered_map<const Music*, AudioProcessor> musicProcessors;
        std::unordered_map<const Sound*, AudioProcessor> soundProcessors;

        // Device enumeration
        ma_device_info* playbackDeviceInfos = nullptr;
        ma_device_info* captureDeviceInfos = nullptr;
        ma_uint32 playbackDeviceCount = 0;
        ma_uint32 captureDeviceCount = 0;

        // FFT data for spectrum analysis
        std::vector<float> fftInput;
        std::vector<std::complex<float>> fftOutput;
    };

    static AudioSystem g_audioSystem;

    // FFT implementation for spectrum analysis
    void FFT(std::vector<std::complex<float>>& data)
    {
        const size_t n = data.size();
        if (n <= 1)
            return;

        // Divide
        std::vector<std::complex<float>> even(n / 2);
        std::vector<std::complex<float>> odd(n / 2);

        for (size_t i = 0; i < n / 2; i++)
        {
            even[i] = data[i * 2];
            odd[i] = data[i * 2 + 1];
        }

        // Conquer
        FFT(even);
        FFT(odd);

        // Combine
        for (size_t k = 0; k < n / 2; k++)
        {
            std::complex<float> t = std::polar(1.0f, -2.0f * (float)M_PI * k / n) * odd[k];
            data[k] = even[k] + t;
            data[k + n / 2] = even[k] - t;
        }
    }

    // Recording callback
    void RecordingDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        if (g_audioSystem.isRecording && pInput)
        {
            float* samples = (float*)pInput;
            size_t sampleCount = frameCount * pDevice->capture.channels;
            g_audioSystem.recordingBuffer.insert(g_audioSystem.recordingBuffer.end(),
                samples, samples + sampleCount);
        }
    }

    // Process echo effect
    void ProcessEcho(EchoEffect& echo, float* buffer, size_t frameCount, unsigned int channels)
    {
        for (size_t i = 0; i < frameCount * channels; i++)
        {
            float input = buffer[i];
            float delayed = echo.delayBuffer[echo.writePos];

            buffer[i] = input * (1.0f - echo.wetDry) + delayed * echo.wetDry;

            echo.delayBuffer[echo.writePos] = input + delayed * echo.feedback;
            echo.writePos = (echo.writePos + 1) % echo.delayBuffer.size();
        }
    }

    // Process reverb effect (Freeverb-style)
    void ProcessReverb(ReverbEffect& reverb, float* buffer, size_t frameCount, unsigned int channels)
    {
        const int combLengths[4] = { 1116, 1188, 1277, 1356 };
        const int allpassLengths[2] = { 556, 441 };

        for (size_t frame = 0; frame < frameCount; frame++)
        {
            for (unsigned int ch = 0; ch < channels; ch++)
            {
                size_t idx = frame * channels + ch;
                float input = buffer[idx];
                float output = 0.0f;

                // Parallel comb filters
                for (int c = 0; c < 4; c++)
                {
                    size_t pos = reverb.combWritePos[c] % reverb.combBuffers[c].size();
                    float delayed = reverb.combBuffers[c][pos];
                    output += delayed;
                    reverb.combBuffers[c][pos] = input + delayed * reverb.damping;
                    reverb.combWritePos[c]++;
                }

                output *= 0.25f;

                // Serial allpass filters
                for (int a = 0; a < 2; a++)
                {
                    size_t pos = reverb.allpassWritePos[a] % reverb.allpassBuffers[a].size();
                    float delayed = reverb.allpassBuffers[a][pos];
                    float temp = output + delayed * 0.5f;
                    reverb.allpassBuffers[a][pos] = output;
                    output = delayed - temp * 0.5f;
                    reverb.allpassWritePos[a]++;
                }

                buffer[idx] = input * (1.0f - reverb.wetDry) + output * reverb.wetDry;
            }
        }
    }

    // Core Audio System Functions
    bool InitAudioDevice()
    {
        AudioConfig config;
        return InitAudioDeviceEx(config);
    }

    bool InitAudioDeviceEx(const AudioConfig& config)
    {
        if (g_audioSystem.initialized)
            return true;

        ma_result result;

        // Initialize context
        result = ma_context_init(nullptr, 0, nullptr, &g_audioSystem.context);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio context" << std::endl;
            return false;
        }

        // Enumerate devices
        result = ma_context_get_devices(&g_audioSystem.context,
            &g_audioSystem.playbackDeviceInfos,
            &g_audioSystem.playbackDeviceCount,
            &g_audioSystem.captureDeviceInfos,
            &g_audioSystem.captureDeviceCount);

        if (result != MA_SUCCESS)
            std::cout << "Audio Warning: Failed to enumerate devices" << std::endl;

        // Initialize engine
        ma_engine_config engineConfig = ma_engine_config_init();
        engineConfig.sampleRate = config.sampleRate;
        engineConfig.channels = config.channels;
        engineConfig.periodSizeInFrames = config.bufferSizeInFrames;
        engineConfig.noAutoStart = MA_FALSE;

        result = ma_engine_init(&engineConfig, &g_audioSystem.engine);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio engine" << std::endl;
            ma_context_uninit(&g_audioSystem.context);
            return false;
        }

        // Set default listener
        ma_engine_listener_set_position(&g_audioSystem.engine, 0, 0.0f, 0.0f, 0.0f);
        ma_engine_listener_set_direction(&g_audioSystem.engine, 0, 0.0f, 0.0f, -1.0f);
        ma_engine_listener_set_world_up(&g_audioSystem.engine, 0, 0.0f, 1.0f, 0.0f);

        g_audioSystem.initialized = true;
        g_audioSystem.masterVolume = 1.0f;

        return true;
    }

    void ShutdownAudioDevice()
    {
        if (!g_audioSystem.initialized)
            return;

        // Stop and clean all active sounds
        for (auto& pair : g_audioSystem.activeSounds)
        {
            if (pair.second)
                ma_sound_uninit(pair.second.get());
        }
        g_audioSystem.activeSounds.clear();

        // Clean processors
        g_audioSystem.musicProcessors.clear();
        g_audioSystem.soundProcessors.clear();

        // Stop recording
        if (g_audioSystem.isRecording)
            StopAudioRecording();

        // Uninitialize engine and context
        ma_engine_uninit(&g_audioSystem.engine);
        ma_context_uninit(&g_audioSystem.context);

        g_audioSystem.initialized = false;
    }

    bool IsAudioDeviceReady()
    {
        return g_audioSystem.initialized;
    }

    void SetMasterVolume(float volume)
    {
        if (!g_audioSystem.initialized)
            return;

        g_audioSystem.masterVolume = std::clamp(volume, 0.0f, 1.0f);
        ma_engine_set_volume(&g_audioSystem.engine, g_audioSystem.masterVolume);
    }

    float GetMasterVolume()
    {
        return g_audioSystem.masterVolume;
    }

    // Device management
    int GetAudioDeviceCount(ma_device_type type)
    {
        if (!g_audioSystem.initialized)
            return 0;

        return (type == ma_device_type_playback) ?
            (int)g_audioSystem.playbackDeviceCount :
            (int)g_audioSystem.captureDeviceCount;
    }

    std::string GetAudioDeviceName(int index, ma_device_type type)
    {
        if (!g_audioSystem.initialized)
            return "";

        if (type == ma_device_type_playback)
        {
            if (index >= 0 && index < (int)g_audioSystem.playbackDeviceCount)
                return g_audioSystem.playbackDeviceInfos[index].name;
        }
        else
        {
            if (index >= 0 && index < (int)g_audioSystem.captureDeviceCount)
                return g_audioSystem.captureDeviceInfos[index].name;
        }

        return "";
    }

    bool SetAudioDevice(int index)
    {
        std::cout << "Audio Warning: Device switching requires reinitialization" << std::endl;
        return false;
    }

    // Sound Loading
    Sound LoadSound(const std::string& fileName)
    {
        Sound sound = {};

        if (!g_audioSystem.initialized)
        {
            std::cout << "Audio Error: Audio system not initialized" << std::endl;
            return sound;
        }

        if (fileName.empty())
        {
            std::cout << "Audio Error: Empty filename" << std::endl;
            return sound;
        }

        // Load and decode entire file
        ma_decoder decoder;
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);

        ma_result result = ma_decoder_init_file(fileName.c_str(), &decoderConfig, &decoder);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to load sound: " << fileName << std::endl;
            return sound;
        }

        ma_uint64 frameCount;
        result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to get frame count for: " << fileName << std::endl;
            ma_decoder_uninit(&decoder);
            return sound;
        }

        sound.frameCount = static_cast<unsigned int>(frameCount);
        sound.channels = decoder.outputChannels;
        sound.sampleRate = decoder.outputSampleRate;
        sound.ownsData = true;

        // Allocate and read PCM data
        sound.pcmData = new float[frameCount * sound.channels];

        ma_uint64 framesRead;
        result = ma_decoder_read_pcm_frames(&decoder, sound.pcmData, frameCount, &framesRead);

        ma_decoder_uninit(&decoder);

        if (result != MA_SUCCESS || framesRead != frameCount)
        {
            std::cout << "Audio Error: Failed to read PCM frames from: " << fileName << std::endl;
            delete[] sound.pcmData;
            return sound;
        }

        // Create audio buffer
        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            ma_format_f32,
            sound.channels,
            frameCount,
            sound.pcmData,
            nullptr
        );

        result = ma_audio_buffer_init(&bufferConfig, &sound.audioBuffer);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio buffer for: " << fileName << std::endl;
            delete[] sound.pcmData;
            return sound;
        }

        sound.valid = true;
        return sound;
    }

    Sound LoadSoundFromWave(const void* data, unsigned int frameCount, unsigned int sampleRate,
        unsigned int channels, ma_format format)
    {
        Sound sound = {};

        if (!g_audioSystem.initialized || !data)
        {
            std::cout << "Audio Error: Invalid parameters for LoadSoundFromWave" << std::endl;
            return sound;
        }

        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            format,
            channels,
            frameCount,
            data,
            nullptr
        );

        ma_result result = ma_audio_buffer_init(&bufferConfig, &sound.audioBuffer);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio buffer from wave data" << std::endl;
            return sound;
        }

        sound.valid = true;
        sound.frameCount = frameCount;
        sound.sampleRate = sampleRate;
        sound.channels = channels;
        sound.ownsData = false;

        return sound;
    }

    bool IsSoundReady(const Sound& sound)
    {
        return sound.valid;
    }

    void UnloadSound(Sound& sound)
    {
        if (!sound.valid)
            return;

        ma_audio_buffer_uninit(&sound.audioBuffer);

        if (sound.ownsData && sound.pcmData)
        {
            delete[] sound.pcmData;
            sound.pcmData = nullptr;
        }

        // Remove any processors
        g_audioSystem.soundProcessors.erase(&sound);

        sound.valid = false;
    }

    // Sound Playback
    void PlaySound(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        auto maSound = std::make_shared<ma_sound>();

        ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;

        ma_audio_buffer* buffer = const_cast<ma_audio_buffer*>(&sound.audioBuffer);
        ma_result result = ma_sound_init_from_data_source(&g_audioSystem.engine,
            buffer,
            flags, nullptr, maSound.get());

        if (result == MA_SUCCESS)
        {
            ma_sound_start(maSound.get());
            size_t id = g_audioSystem.nextSoundId++;
            g_audioSystem.activeSounds[id] = maSound;
        }
        else
            std::cout << "Audio Error: Failed to play sound" << std::endl;
    }

    void PlaySoundMulti(const Sound& sound)
    {
        PlaySound(sound);
    }

    void StopSound(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        // Stop all instances
        auto it = g_audioSystem.activeSounds.begin();
        while (it != g_audioSystem.activeSounds.end())
        {
            if (ma_sound_is_playing(it->second.get()))
                ma_sound_stop(it->second.get());

            ++it;
        }
    }

    void PauseSound(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
        {
            if (ma_sound_is_playing(pair.second.get()))
                ma_sound_stop(pair.second.get());
        }
    }

    void ResumeSound(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_start(pair.second.get());
    }

    bool IsSoundPlaying(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return false;

        for (auto& pair : g_audioSystem.activeSounds)
        {
            if (ma_sound_is_playing(pair.second.get()))
                return true;
        }

        return false;
    }

    // Sound Properties
    void SetSoundVolume(const Sound& sound, float volume)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        volume = std::clamp(volume, 0.0f, 1.0f);
        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_volume(pair.second.get(), volume);
    }

    void SetSoundPitch(const Sound& sound, float pitch)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        pitch = std::max(0.1f, pitch);
        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_pitch(pair.second.get(), pitch);
    }

    void SetSoundPan(const Sound& sound, float pan)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        pan = std::clamp(pan, 0.0f, 1.0f);
        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_pan(pair.second.get(), pan);
    }

    // Music Loading
    Music LoadMusicStream(const std::string& fileName)
    {
        Music music = {};

        if (!g_audioSystem.initialized)
        {
            std::cout << "Audio Error: Audio system not initialized" << std::endl;
            return music;
        }

        if (fileName.empty())
        {
            std::cout << "Audio Error: Empty filename" << std::endl;
            return music;
        }

        // Initialize streaming sound
        ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_result result = ma_sound_init_from_file(&g_audioSystem.engine, fileName.c_str(),
            flags, nullptr, nullptr, &music.sound);

        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to load music stream: " << fileName << std::endl;
            return music;
        }

        // Create separate decoder for length/format queries
        music.decoder = new ma_decoder();
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
        result = ma_decoder_init_file(fileName.c_str(), &decoderConfig, music.decoder);

        if (result == MA_SUCCESS)
        {
            music.sampleRate = music.decoder->outputSampleRate;
            music.channels = music.decoder->outputChannels;
        }
        else
        {
            std::cout << "Audio Warning: Failed to create decoder for queries" << std::endl;
            delete music.decoder;
            music.decoder = nullptr;
        }

        music.valid = true;
        music.volume = 1.0f;
        music.pitch = 1.0f;
        music.pan = 0.5f;
        music.looping = false;
        music.filePath = fileName;

        return music;
    }

    bool IsMusicReady(const Music& music)
    {
        return music.valid;
    }

    void UnloadMusicStream(Music& music)
    {
        if (!music.valid)
            return;

        if (music.isPlaying)
            ma_sound_stop(&music.sound);

        ma_sound_uninit(&music.sound);

        if (music.decoder)
        {
            ma_decoder_uninit(music.decoder);
            delete music.decoder;
            music.decoder = nullptr;
        }

        g_audioSystem.musicProcessors.erase(&music);

        music.valid = false;
    }

    // Music Playback
    void PlayMusicStream(Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_start(&music.sound);
        music.isPlaying = true;
        music.isPaused = false;
    }

    void StopMusicStream(Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_stop(&music.sound);
        ma_sound_seek_to_pcm_frame(&music.sound, 0);
        music.isPlaying = false;
        music.isPaused = false;
    }

    void PauseMusicStream(Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        if (music.isPlaying && !music.isPaused)
        {
            ma_sound_stop(&music.sound);
            music.isPaused = true;
        }
    }

    void ResumeMusicStream(Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        if (music.isPaused)
        {
            ma_sound_start(&music.sound);
            music.isPaused = false;
        }
    }

    void UpdateMusicStream(Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        // Check if finished
        if (music.isPlaying && !ma_sound_is_playing(&music.sound))
        {
            if (music.looping)
            {
                ma_sound_seek_to_pcm_frame(&music.sound, 0);
                ma_sound_start(&music.sound);
            }
            else
            {
                music.isPlaying = false;
                if (music.onFinishCallback)
                    music.onFinishCallback();
            }
        }
    }

    bool IsMusicStreamPlaying(const Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return false;

        return ma_sound_is_playing(&music.sound);
    }

    // Music Properties
    void SetMusicVolume(Music& music, float volume)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        volume = std::clamp(volume, 0.0f, 1.0f);
        music.volume = volume;
        ma_sound_set_volume(&music.sound, volume);
    }

    void SetMusicPitch(Music& music, float pitch)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        pitch = std::max(0.1f, pitch);
        music.pitch = pitch;
        ma_sound_set_pitch(&music.sound, pitch);
    }

    void SetMusicPan(Music& music, float pan)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        pan = std::clamp(pan, 0.0f, 1.0f);
        music.pan = pan;
        ma_sound_set_pan(&music.sound, pan);
    }

    void SetMusicLooping(Music& music, bool loop)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        music.looping = loop;
        ma_sound_set_looping(&music.sound, loop ? MA_TRUE : MA_FALSE);
    }

    float GetMusicTimeLength(const Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid || !music.decoder)
            return 0.0f;

        ma_uint64 lengthInFrames;
        ma_result result = ma_decoder_get_length_in_pcm_frames(music.decoder, &lengthInFrames);

        if (result != MA_SUCCESS)
            return 0.0f;

        return (float)lengthInFrames / (float)music.sampleRate;
    }

    float GetMusicTimePlayed(const Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return 0.0f;

        ma_uint64 cursorInFrames;
        ma_result result = ma_sound_get_cursor_in_pcm_frames(&music.sound, &cursorInFrames);

        if (result != MA_SUCCESS)
            return 0.0f;

        ma_uint32 sampleRate;
        ma_sound_get_data_format(&music.sound, nullptr, nullptr, &sampleRate, nullptr, 0);

        return (float)cursorInFrames / (float)sampleRate;
    }

    void SeekMusicStream(Music& music, float position)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_uint32 sampleRate;
        ma_sound_get_data_format(&music.sound, nullptr, nullptr, &sampleRate, nullptr, 0);

        ma_uint64 framePosition = (ma_uint64)(position * sampleRate);
        ma_sound_seek_to_pcm_frame(&music.sound, framePosition);
    }

    void SetMusicFinishedCallback(Music& music, std::function<void()> callback)
    {
        if (!music.valid)
            return;

        music.onFinishCallback = callback;
    }

    // Audio Stream custom data source callbacks
    ma_result AudioStreamRead(ma_data_source* pDataSource, void* pFramesOut,
        ma_uint64 frameCount, ma_uint64* pFramesRead)
    {
        AudioStream* stream = (AudioStream*)pDataSource;

        ma_uint32 framesRead = 0;
        void* mappedBuffer;

        ma_pcm_rb_acquire_read(&stream->buffer, (ma_uint32*)&frameCount, &mappedBuffer);

        if (mappedBuffer && frameCount > 0)
        {
            size_t bytesPerFrame = ma_get_bytes_per_frame(stream->format, stream->channels);
            memcpy(pFramesOut, mappedBuffer, frameCount * bytesPerFrame);
            ma_pcm_rb_commit_read(&stream->buffer, (ma_uint32)frameCount);
            framesRead = (ma_uint32)frameCount;
        }

        if (pFramesRead)
            *pFramesRead = framesRead;

        return MA_SUCCESS;
    }

    ma_result AudioStreamSeek(ma_data_source* pDataSource, ma_uint64 frameIndex)
    {
        return MA_NOT_IMPLEMENTED;
    }

    ma_result AudioStreamGetDataFormat(ma_data_source* pDataSource, ma_format* pFormat,
        ma_uint32* pChannels, ma_uint32* pSampleRate,
        ma_channel* pChannelMap, size_t channelMapCap)
    {
        AudioStream* stream = (AudioStream*)pDataSource;

        if (pFormat)
            *pFormat = stream->format;
        if (pChannels)
            *pChannels = stream->channels;
        if (pSampleRate)
            *pSampleRate = stream->sampleRate;

        return MA_SUCCESS;
    }

    ma_result AudioStreamGetCursor(ma_data_source* pDataSource, ma_uint64* pCursor)
    {
        *pCursor = 0;
        return MA_SUCCESS;
    }

    ma_result AudioStreamGetLength(ma_data_source* pDataSource, ma_uint64* pLength)
    {
        *pLength = 0;
        return MA_NOT_IMPLEMENTED;
    }

    // Audio Stream custom data source vtable
    static ma_data_source_vtable g_audioStreamVTable =
    {
        AudioStreamRead,
        AudioStreamSeek,
        AudioStreamGetDataFormat,
        AudioStreamGetCursor,
        AudioStreamGetLength,
        nullptr, // onSetLooping
        0 // flags
    };

    // Audio Stream Functions
    AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int channels, ma_format format)
    {
        AudioStream stream = {};

        if (!g_audioSystem.initialized)
        {
            std::cout << "Audio Error: Audio system not initialized" << std::endl;
            return stream;
        }

        stream.sampleRate = sampleRate;
        stream.channels = channels;
        stream.format = format;
        stream.bufferSizeInFrames = sampleRate; // 1 second buffer

        // Initialize ring buffer
        ma_uint32 subbufferSizeInFrames = stream.bufferSizeInFrames / 3;
        ma_result result = ma_pcm_rb_init(format, channels, subbufferSizeInFrames,
            nullptr, nullptr, &stream.buffer);

        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio stream buffer" << std::endl;
            return stream;
        }

        // Setup data source
        ma_data_source_config dsConfig = ma_data_source_config_init();
        dsConfig.vtable = &g_audioStreamVTable;

        ma_data_source_init(&dsConfig, &stream.ds);

        // Initialize sound from data source
        result = ma_sound_init_from_data_source(&g_audioSystem.engine, &stream.ds,
            0, nullptr, &stream.sound);

        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize audio stream sound" << std::endl;
            ma_pcm_rb_uninit(&stream.buffer);
            return stream;
        }

        stream.valid = true;
        return stream;
    }

    void UnloadAudioStream(AudioStream& stream)
    {
        if (!stream.valid)
            return;

        ma_sound_uninit(&stream.sound);
        ma_pcm_rb_uninit(&stream.buffer);
        stream.valid = false;
    }

    void UpdateAudioStream(AudioStream& stream, const void* data, unsigned int frameCount)
    {
        if (!g_audioSystem.initialized || !stream.valid || !data)
            return;

        void* mappedBuffer;
        ma_uint32 framesToWrite = frameCount;

        ma_pcm_rb_acquire_write(&stream.buffer, &framesToWrite, &mappedBuffer);

        if (mappedBuffer && framesToWrite > 0)
        {
            size_t bytesPerFrame = ma_get_bytes_per_frame(stream.format, stream.channels);
            memcpy(mappedBuffer, data, framesToWrite * bytesPerFrame);
            ma_pcm_rb_commit_write(&stream.buffer, framesToWrite);
        }
    }

    bool IsAudioStreamProcessed(const AudioStream& stream)
    {
        if (!stream.valid)
            return false;

        ma_pcm_rb* buffer = const_cast<ma_pcm_rb*>(&stream.buffer);
        ma_uint32 availableFrames = ma_pcm_rb_available_write(buffer);
        return availableFrames > (stream.bufferSizeInFrames / 2);
    }

    void PlayAudioStream(AudioStream& stream)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        ma_sound_start(&stream.sound);
    }

    void PauseAudioStream(AudioStream& stream)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        ma_sound_stop(&stream.sound);
    }

    void ResumeAudioStream(AudioStream& stream)
    {
        PlayAudioStream(stream);
    }

    bool IsAudioStreamPlaying(const AudioStream& stream)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return false;

        return ma_sound_is_playing(&stream.sound);
    }

    void StopAudioStream(AudioStream& stream)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        ma_sound_stop(&stream.sound);
    }

    void SetAudioStreamVolume(AudioStream& stream, float volume)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        volume = std::clamp(volume, 0.0f, 1.0f);
        ma_sound_set_volume(&stream.sound, volume);
    }

    void SetAudioStreamPitch(AudioStream& stream, float pitch)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        pitch = std::max(0.1f, pitch);
        ma_sound_set_pitch(&stream.sound, pitch);
    }

    void SetAudioStreamPan(AudioStream& stream, float pan)
    {
        if (!g_audioSystem.initialized || !stream.valid)
            return;

        pan = std::clamp(pan, 0.0f, 1.0f);
        ma_sound_set_pan(&stream.sound, pan);
    }

    // 3D Audio Listener
    void SetAudioListenerPosition(float x, float y, float z)
    {
        if (!g_audioSystem.initialized)
            return;

        g_audioSystem.listener.positionX = x;
        g_audioSystem.listener.positionY = y;
        g_audioSystem.listener.positionZ = z;

        ma_engine_listener_set_position(&g_audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerDirection(float x, float y, float z)
    {
        if (!g_audioSystem.initialized)
            return;

        g_audioSystem.listener.directionX = x;
        g_audioSystem.listener.directionY = y;
        g_audioSystem.listener.directionZ = z;

        ma_engine_listener_set_direction(&g_audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerVelocity(float x, float y, float z)
    {
        if (!g_audioSystem.initialized)
            return;

        g_audioSystem.listener.velocityX = x;
        g_audioSystem.listener.velocityY = y;
        g_audioSystem.listener.velocityZ = z;

        ma_engine_listener_set_velocity(&g_audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerOrientation(float dirX, float dirY, float dirZ,
        float upX, float upY, float upZ)
    {
        if (!g_audioSystem.initialized)
            return;

        SetAudioListenerDirection(dirX, dirY, dirZ);

        g_audioSystem.listener.worldUpX = upX;
        g_audioSystem.listener.worldUpY = upY;
        g_audioSystem.listener.worldUpZ = upZ;

        ma_engine_listener_set_world_up(&g_audioSystem.engine, 0, upX, upY, upZ);
    }

    void SetAudioListener(const AudioListener& listener)
    {
        g_audioSystem.listener = listener;
        SetAudioListenerPosition(listener.positionX, listener.positionY, listener.positionZ);
        SetAudioListenerDirection(listener.directionX, listener.directionY, listener.directionZ);
        SetAudioListenerVelocity(listener.velocityX, listener.velocityY, listener.velocityZ);
        SetAudioListenerOrientation(listener.directionX, listener.directionY, listener.directionZ, listener.worldUpX, listener.worldUpY, listener.worldUpZ);
    }

    AudioListener GetAudioListener()
    {
        return g_audioSystem.listener;
    }

    // 3D Audio Sound
    void SetSoundPosition(const Sound& sound, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_position(pair.second.get(), x, y, z);
    }

    void SetSoundVelocity(const Sound& sound, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_velocity(pair.second.get(), x, y, z);
    }

    void SetSoundDirection(const Sound& sound, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_direction(pair.second.get(), x, y, z);
    }

    void SetSoundCone(const Sound& sound, float innerAngle, float outerAngle, float outerGain)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_cone(pair.second.get(), innerAngle, outerAngle, outerGain);
    }

    void SetSoundAttenuation(const Sound& sound, ma_attenuation_model model,
        float minDistance, float maxDistance, float rolloff)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
        {
            ma_sound_set_attenuation_model(pair.second.get(), model);
            ma_sound_set_min_distance(pair.second.get(), minDistance);
            ma_sound_set_max_distance(pair.second.get(), maxDistance);
            ma_sound_set_rolloff(pair.second.get(), rolloff);
        }
    }

    void SetSound3DConfig(const Sound& sound, const Audio3DConfig& config)
    {
        SetSoundPosition(sound, config.positionX, config.positionY, config.positionZ);
        SetSoundVelocity(sound, config.velocityX, config.velocityY, config.velocityZ);
        SetSoundDirection(sound, config.directionX, config.directionY, config.directionZ);
        SetSoundCone(sound, config.coneInnerAngle, config.coneOuterAngle, config.coneOuterGain);
        SetSoundAttenuation(sound, config.attenuationModel, config.minDistance,
            config.maxDistance, config.rolloff);
        SetSoundDopplerFactor(sound, config.dopplerFactor);
        SetSoundPositioning(sound, config.positioning);
    }

    void SetSoundSpatialization(const Sound& sound, bool enable)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_spatialization_enabled(pair.second.get(), enable ? MA_TRUE : MA_FALSE);
    }

    void SetSoundDopplerFactor(const Sound& sound, float factor)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_doppler_factor(pair.second.get(), factor);
    }

    void SetSoundPositioning(const Sound& sound, ma_positioning mode)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        for (auto& pair : g_audioSystem.activeSounds)
            ma_sound_set_positioning(pair.second.get(), mode);
    }

    // 3D Audio Music
    void SetMusicPosition(Music& music, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_position(&music.sound, x, y, z);
    }

    void SetMusicVelocity(Music& music, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_velocity(&music.sound, x, y, z);
    }

    void SetMusicDirection(Music& music, float x, float y, float z)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_direction(&music.sound, x, y, z);
    }

    void SetMusicCone(Music& music, float innerAngle, float outerAngle, float outerGain)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_cone(&music.sound, innerAngle, outerAngle, outerGain);
    }

    void SetMusicAttenuation(Music& music, ma_attenuation_model model,
        float minDistance, float maxDistance, float rolloff)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_attenuation_model(&music.sound, model);
        ma_sound_set_min_distance(&music.sound, minDistance);
        ma_sound_set_max_distance(&music.sound, maxDistance);
        ma_sound_set_rolloff(&music.sound, rolloff);
    }

    void SetMusic3DConfig(Music& music, const Audio3DConfig& config)
    {
        SetMusicPosition(music, config.positionX, config.positionY, config.positionZ);
        SetMusicVelocity(music, config.velocityX, config.velocityY, config.velocityZ);
        SetMusicDirection(music, config.directionX, config.directionY, config.directionZ);
        SetMusicCone(music, config.coneInnerAngle, config.coneOuterAngle, config.coneOuterGain);
        SetMusicAttenuation(music, config.attenuationModel, config.minDistance, config.maxDistance, config.rolloff);
        SetMusicDopplerFactor(music, config.dopplerFactor);
        SetMusicPositioning(music, config.positioning);
    }

    void SetMusicSpatialization(Music& music, bool enable)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_spatialization_enabled(&music.sound, enable ? MA_TRUE : MA_FALSE);
    }

    void SetMusicDopplerFactor(Music& music, float factor)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_doppler_factor(&music.sound, factor);
    }

    void SetMusicPositioning(Music& music, ma_positioning mode)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        ma_sound_set_positioning(&music.sound, mode);
    }

    // Audio Effects Sound
    void SetSoundEffect(const Sound& sound, AudioEffect effect, float param1, float param2)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return;

        AudioProcessor& processor = g_audioSystem.soundProcessors[&sound];
        processor.activeEffect = effect;
        processor.enabled = true;

        ma_format format = ma_format_f32;
        ma_uint32 sampleRate = sound.sampleRate;
        ma_uint32 channels = sound.channels;

        switch (effect)
        {
            case AUDIO_EFFECT_LOWPASS:
            {
                ma_lpf_config config = ma_lpf_config_init(format, channels, sampleRate, param1, 2);
                ma_lpf_init(&config, nullptr, &processor.lpf);
                break;
            }
            case AUDIO_EFFECT_HIGHPASS:
            {
                ma_hpf_config config = ma_hpf_config_init(format, channels, sampleRate, param1, 2);
                ma_hpf_init(&config, nullptr, &processor.hpf);
                break;
            }
            case AUDIO_EFFECT_BANDPASS:
            {
                ma_bpf_config config = ma_bpf_config_init(format, channels, sampleRate, param1, 2);
                ma_bpf_init(&config, nullptr, &processor.bpf);
                break;
            }
            case AUDIO_EFFECT_NOTCH:
            {
                ma_notch2_config config = ma_notch2_config_init(format, channels, sampleRate,
                    param1, param2);
                ma_notch2_init(&config, nullptr, &processor.notch);
                break;
            }
            case AUDIO_EFFECT_PEAKING:
            {
                ma_peak2_config config = ma_peak2_config_init(format, channels, sampleRate,
                    param1, 0.707, param2);
                ma_peak2_init(&config, nullptr, &processor.peak);
                break;
            }
            case AUDIO_EFFECT_LOSHELF:
            {
                ma_loshelf2_config config = ma_loshelf2_config_init(format, channels, sampleRate,
                    param1, 0.707, param2);
                ma_loshelf2_init(&config, nullptr, &processor.loshelf);
                break;
            }
            case AUDIO_EFFECT_HISHELF:
            {
                ma_hishelf2_config config = ma_hishelf2_config_init(format, channels, sampleRate,
                    param1, 0.707, param2);
                ma_hishelf2_init(&config, nullptr, &processor.hishelf);
                break;
            }
            case AUDIO_EFFECT_ECHO:
            {
                // param1 = delay time in seconds, param2 = feedback amount
                processor.echo.delaySamples = (unsigned int)(param1 * sampleRate * channels);
                processor.echo.delayBuffer.resize(processor.echo.delaySamples, 0.0f);
                processor.echo.writePos = 0;
                processor.echo.feedback = std::clamp(param2, 0.0f, 0.95f);
                processor.echo.wetDry = 0.5f;
                break;
            }
            case AUDIO_EFFECT_REVERB:
            {
                // param1 = room size, param2 = damping
                processor.reverb.roomSize = std::clamp(param1, 0.0f, 1.0f);
                processor.reverb.damping = std::clamp(param2, 0.0f, 1.0f);
                processor.reverb.wetDry = 0.3f;

                // Initialize comb filters
                const int combLengths[4] = { 1116, 1188, 1277, 1356 };
                for (int i = 0; i < 4; i++)
                {
                    int length = (int)(combLengths[i] * processor.reverb.roomSize);
                    processor.reverb.combBuffers[i].resize(length * channels, 0.0f);
                    processor.reverb.combWritePos[i] = 0;
                }

                // Initialize allpass filters
                const int allpassLengths[2] = { 556, 441 };
                for (int i = 0; i < 2; i++)
                {
                    processor.reverb.allpassBuffers[i].resize(allpassLengths[i] * channels, 0.0f);
                    processor.reverb.allpassWritePos[i] = 0;
                }
                break;
            }
            default:
                processor.enabled = false;
                break;
        }
    }

    void RemoveSoundEffect(const Sound& sound)
    {
        if (!sound.valid)
            return;

        g_audioSystem.soundProcessors.erase(&sound);
    }

    // Audio Effects Music
    void SetMusicEffect(Music& music, AudioEffect effect, float param1, float param2)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return;

        AudioProcessor& processor = g_audioSystem.musicProcessors[&music];
        processor.activeEffect = effect;
        processor.enabled = true;

        ma_format format;
        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_sound_get_data_format(&music.sound, &format, &channels, &sampleRate, nullptr, 0);

        switch (effect)
        {
        case AUDIO_EFFECT_LOWPASS:
        {
            ma_lpf_config config = ma_lpf_config_init(format, channels, sampleRate, param1, 2);
            ma_lpf_init(&config, nullptr, &processor.lpf);
            break;
        }
        case AUDIO_EFFECT_HIGHPASS:
        {
            ma_hpf_config config = ma_hpf_config_init(format, channels, sampleRate, param1, 2);
            ma_hpf_init(&config, nullptr, &processor.hpf);
            break;
        }
        case AUDIO_EFFECT_BANDPASS:
        {
            ma_bpf_config config = ma_bpf_config_init(format, channels, sampleRate, param1, 2);
            ma_bpf_init(&config, nullptr, &processor.bpf);
            break;
        }
        case AUDIO_EFFECT_NOTCH:
        {
            ma_notch2_config config = ma_notch2_config_init(format, channels, sampleRate,
                param1, param2);
            ma_notch2_init(&config, nullptr, &processor.notch);
            break;
        }
        case AUDIO_EFFECT_PEAKING:
        {
            ma_peak2_config config = ma_peak2_config_init(format, channels, sampleRate,
                param1, 0.707, param2);
            ma_peak2_init(&config, nullptr, &processor.peak);
            break;
        }
        case AUDIO_EFFECT_LOSHELF:
        {
            ma_loshelf2_config config = ma_loshelf2_config_init(format, channels, sampleRate,
                param1, 0.707, param2);
            ma_loshelf2_init(&config, nullptr, &processor.loshelf);
            break;
        }
        case AUDIO_EFFECT_HISHELF:
        {
            ma_hishelf2_config config = ma_hishelf2_config_init(format, channels, sampleRate,
                param1, 0.707, param2);
            ma_hishelf2_init(&config, nullptr, &processor.hishelf);
            break;
        }
        case AUDIO_EFFECT_ECHO:
        {
            processor.echo.delaySamples = (unsigned int)(param1 * sampleRate * channels);
            processor.echo.delayBuffer.resize(processor.echo.delaySamples, 0.0f);
            processor.echo.writePos = 0;
            processor.echo.feedback = std::clamp(param2, 0.0f, 0.95f);
            processor.echo.wetDry = 0.5f;
            break;
        }
        case AUDIO_EFFECT_REVERB:
        {
            processor.reverb.roomSize = std::clamp(param1, 0.0f, 1.0f);
            processor.reverb.damping = std::clamp(param2, 0.0f, 1.0f);
            processor.reverb.wetDry = 0.3f;

            const int combLengths[4] = { 1116, 1188, 1277, 1356 };
            for (int i = 0; i < 4; i++)
            {
                int length = (int)(combLengths[i] * processor.reverb.roomSize);
                processor.reverb.combBuffers[i].resize(length * channels, 0.0f);
                processor.reverb.combWritePos[i] = 0;
            }

            const int allpassLengths[2] = { 556, 441 };
            for (int i = 0; i < 2; i++)
            {
                processor.reverb.allpassBuffers[i].resize(allpassLengths[i] * channels, 0.0f);
                processor.reverb.allpassWritePos[i] = 0;
            }
            break;
        }
        default:
            processor.enabled = false;
            break;
        }
    }

    void RemoveMusicEffect(Music& music)
    {
        if (!music.valid)
            return;

        g_audioSystem.musicProcessors.erase(&music);
    }

    // Audio Recording
    bool StartAudioRecording(unsigned int sampleRate, unsigned int channels)
    {
        if (!g_audioSystem.initialized || g_audioSystem.isRecording)
        {
            std::cout << "Audio Error: Cannot start recording" << std::endl;
            return false;
        }

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
        deviceConfig.capture.format = ma_format_f32;
        deviceConfig.capture.channels = channels;
        deviceConfig.sampleRate = sampleRate;
        deviceConfig.dataCallback = RecordingDataCallback;
        deviceConfig.pUserData = nullptr;

        ma_result result = ma_device_init(&g_audioSystem.context, &deviceConfig, &g_audioSystem.recordingDevice);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize recording device" << std::endl;
            return false;
        }

        result = ma_device_start(&g_audioSystem.recordingDevice);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to start recording device" << std::endl;
            ma_device_uninit(&g_audioSystem.recordingDevice);
            return false;
        }

        g_audioSystem.isRecording = true;
        g_audioSystem.recordingBuffer.clear();

        return true;
    }

    void StopAudioRecording()
    {
        if (!g_audioSystem.isRecording)
            return;

        ma_device_stop(&g_audioSystem.recordingDevice);
        ma_device_uninit(&g_audioSystem.recordingDevice);
        g_audioSystem.isRecording = false;
    }

    bool IsRecordingAudio()
    {
        return g_audioSystem.isRecording;
    }

    std::vector<float> GetRecordedAudio()
    {
        return g_audioSystem.recordingBuffer;
    }

    void SaveRecordedAudio(const std::string& fileName)
    {
        if (g_audioSystem.recordingBuffer.empty() || fileName.empty())
        {
            std::cout << "Audio Error: No recorded audio or invalid filename" << std::endl;
            return;
        }

        ma_encoder_config config = ma_encoder_config_init(
            ma_encoding_format_wav,
            ma_format_f32,
            g_audioSystem.recordingDevice.capture.channels,
            g_audioSystem.recordingDevice.sampleRate
        );

        ma_encoder encoder;
        ma_result result = ma_encoder_init_file(fileName.c_str(), &config, &encoder);
        if (result != MA_SUCCESS)
        {
            std::cout << "Audio Error: Failed to initialize encoder" << std::endl;
            return;
        }

        ma_uint64 framesToWrite = g_audioSystem.recordingBuffer.size() / g_audioSystem.recordingDevice.capture.channels;
        ma_uint64 framesWritten = 0;

        result = ma_encoder_write_pcm_frames(&encoder,
            g_audioSystem.recordingBuffer.data(),
            framesToWrite,
            &framesWritten);

        if (result != MA_SUCCESS)
            std::cout << "Audio Error: Failed to write recorded audio" << std::endl;
        else
            std::cout << "Audio System: Saved recording to " << fileName << std::endl;

        ma_encoder_uninit(&encoder);
    }

    // Waveform Generation
    Sound GenerateSoundWave(int waveType, float frequency, float duration,
        unsigned int sampleRate)
    {
        if (!g_audioSystem.initialized)
        {
            std::cout << "Audio Error: Audio system not initialized" << std::endl;
            return Sound{};
        }

        unsigned int frameCount = (unsigned int)(duration * sampleRate);
        float* samples = new float[frameCount];

        float period = sampleRate / frequency;

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (unsigned int i = 0; i < frameCount; i++)
        {
            float t = (float)i / period;

            switch (waveType)
            {
                case WAVE_SINE:
                    samples[i] = sinf(2.0f * (float)M_PI * t);
                    break;
                case WAVE_SQUARE:
                    samples[i] = (sinf(2.0f * (float)M_PI * t) >= 0.0f) ? 1.0f : -1.0f;
                    break;
                case WAVE_TRIANGLE:
                    samples[i] = 2.0f * fabsf(2.0f * (t - floorf(t + 0.5f))) - 1.0f;
                    break;
                case WAVE_SAWTOOTH:
                    samples[i] = 2.0f * (t - floorf(t + 0.5f));
                    break;
                case WAVE_NOISE:
                    samples[i] = dist(gen);
                    break;
                default:
                    samples[i] = 0.0f;
                    break;
            }
        }

        Sound sound = LoadSoundFromWave(samples, frameCount, sampleRate, 1, ma_format_f32);

        // Mark that this sound owns its data
        if (sound.valid)
        {
            sound.pcmData = samples;
            sound.ownsData = true;
        }
        else
            delete[] samples;

        return sound;
    }

    void UpdateSoundWave(Sound& sound, int waveType, float frequency)
    {
        if (!sound.valid || !sound.pcmData || !sound.ownsData)
        {
            std::cout << "Audio Error: Cannot update sound wave - invalid sound" << std::endl;
            return;
        }

        float period = (float)sound.sampleRate / frequency;

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (unsigned int i = 0; i < sound.frameCount; i++)
        {
            float t = (float)i / period;

            switch (waveType)
            {
                case WAVE_SINE:
                    sound.pcmData[i] = sinf(2.0f * (float)M_PI * t);
                    break;
                case WAVE_SQUARE:
                    sound.pcmData[i] = (sinf(2.0f * (float)M_PI * t) >= 0.0f) ? 1.0f : -1.0f;
                    break;
                case WAVE_TRIANGLE:
                    sound.pcmData[i] = 2.0f * fabsf(2.0f * (t - floorf(t + 0.5f))) - 1.0f;
                    break;
                case WAVE_SAWTOOTH:
                    sound.pcmData[i] = 2.0f * (t - floorf(t + 0.5f));
                    break;
                case WAVE_NOISE:
                    sound.pcmData[i] = dist(gen);
                    break;
                default:
                    sound.pcmData[i] = 0.0f;
                    break;
            }
        }

        // Reinitialize the audio buffer with updated data
        ma_audio_buffer_uninit(&sound.audioBuffer);

        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            ma_format_f32,
            sound.channels,
            sound.frameCount,
            sound.pcmData,
            nullptr
        );

        ma_result result = ma_audio_buffer_init(&bufferConfig, &sound.audioBuffer);
        if (result != MA_SUCCESS)
            std::cout << "Audio Error: Failed to reinitialize audio buffer" << std::endl;
    }

    // Audio Analysis
    float GetSoundVolume(const Sound& sound)
    {
        if (!g_audioSystem.initialized || !sound.valid)
            return 0.0f;

        // Return volume from first active instance
        for (auto& pair : g_audioSystem.activeSounds)
            return ma_sound_get_volume(pair.second.get());

        return 0.0f;
    }

    float GetMusicVolume(const Music& music)
    {
        if (!g_audioSystem.initialized || !music.valid)
            return 0.0f;

        return music.volume;
    }

    std::vector<float> GetAudioSpectrumData(int sampleCount)
    {
        std::vector<float> spectrum(sampleCount, 0.0f);

        if (!g_audioSystem.initialized)
            return spectrum;

        // Ensure sample count is power of 2 for FFT
        int fftSize = 1;
        while (fftSize < sampleCount)
            fftSize *= 2;

        // Capture current audio output 
        // Todo: We need to use the audio pipeline
        if (g_audioSystem.fftInput.size() != fftSize)
        {
            g_audioSystem.fftInput.resize(fftSize, 0.0f);
            g_audioSystem.fftOutput.resize(fftSize);
        }

        // Convert to complex numbers
        for (int i = 0; i < fftSize; i++)
        {
            if (i < (int)g_audioSystem.fftInput.size())
                g_audioSystem.fftOutput[i] = std::complex<float>(g_audioSystem.fftInput[i], 0.0f);
            else
                g_audioSystem.fftOutput[i] = std::complex<float>(0.0f, 0.0f);
        }

        // Perform FFT
        FFT(g_audioSystem.fftOutput);

        // Calculate magnitude spectrum
        for (int i = 0; i < sampleCount && i < fftSize / 2; i++)
        {
            float real = g_audioSystem.fftOutput[i].real();
            float imag = g_audioSystem.fftOutput[i].imag();
            spectrum[i] = sqrtf(real * real + imag * imag) / fftSize;
        }

        return spectrum;
    }

    // Utility Functions
    std::string GetAudioFormatName(ma_format format)
    {
        switch (format)
        {
            case ma_format_u8:
                return "8-bit unsigned";
            case ma_format_s16:
                return "16-bit signed";
            case ma_format_s24:
                return "24-bit signed";
            case ma_format_s32:
                return "32-bit signed";
            case ma_format_f32:
                return "32-bit float";
            default:
                return "Unknown";
        }
    }

    unsigned int GetAudioFormatSize(ma_format format)
    {
        return ma_get_bytes_per_sample(format);
    }
}