#pragma once

#define NOMINMAX
#include "miniaudio/include/miniaudio.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <complex>

namespace cl
{
    // Forward declarations
    struct Sound;
    struct Music;
    struct AudioStream;

    // Audio configuration structure
    struct AudioConfig
    {
        unsigned int sampleRate = 48000;
        unsigned int channels = 2;
        ma_format format = ma_format_f32;
        unsigned int bufferSizeInFrames = 0; // 0 = auto
        ma_device_type deviceType = ma_device_type_playback;
    };

    // 3D Audio listener configuration
    struct AudioListener
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = -1.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float velocityZ = 0.0f;
        float worldUpX = 0.0f;
        float worldUpY = 1.0f;
        float worldUpZ = 0.0f;
        float coneInnerAngle = 6.283185f; // 360 degrees
        float coneOuterAngle = 6.283185f;
        float coneOuterGain = 0.0f;
    };

    // 3D Audio source configuration
    struct Audio3DConfig
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float velocityZ = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float coneInnerAngle = 6.283185f;
        float coneOuterAngle = 6.283185f;
        float coneOuterGain = 0.0f;
        float dopplerFactor = 1.0f;
        float minGain = 0.0f;
        float maxGain = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 340.29f; // Speed of sound
        float rolloff = 1.0f;
        ma_attenuation_model attenuationModel = ma_attenuation_model_inverse;
        ma_positioning positioning = ma_positioning_relative;
    };

    // Audio effects
    enum AudioEffect
    {
        AUDIO_EFFECT_NONE = 0,
        AUDIO_EFFECT_REVERB,
        AUDIO_EFFECT_ECHO,
        AUDIO_EFFECT_LOWPASS,
        AUDIO_EFFECT_HIGHPASS,
        AUDIO_EFFECT_BANDPASS,
        AUDIO_EFFECT_NOTCH,
        AUDIO_EFFECT_PEAKING,
        AUDIO_EFFECT_LOSHELF,
        AUDIO_EFFECT_HISHELF
    };

    // Sound structure
    struct Sound
    {
        ma_audio_buffer audioBuffer;
        ma_sound* soundInstance = nullptr; // For spatial/3D sounds
        float* pcmData = nullptr;
        bool valid = false;
        unsigned int frameCount = 0;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
        bool ownsData = false; // Track if we need to delete pcmData
    };

    // Music stream structure
    struct Music
    {
        ma_sound sound;
        ma_decoder* decoder = nullptr; // Separate decoder for queries
        bool valid = false;
        bool isPlaying = false;
        bool isPaused = false;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
        float volume = 1.0f;
        float pitch = 1.0f;
        float pan = 0.5f;
        bool looping = false;
        std::string filePath; // Store for decoder queries
        std::function<void()> onFinishCallback = nullptr;
    };

    // Audio stream for custom PCM data
    struct AudioStream
    {
        ma_data_source_base ds;
        ma_pcm_rb buffer;
        ma_sound sound;
        bool valid = false;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
        ma_format format = ma_format_f32;
        unsigned int bufferSizeInFrames = 0;
    };

    // Echo effect parameters
    struct EchoEffect
    {
        std::vector<float> delayBuffer;
        size_t writePos = 0;
        unsigned int delaySamples = 0;
        float feedback = 0.5f;
        float wetDry = 0.5f;
    };

    // Reverb effect parameters
    struct ReverbEffect
    {
        std::vector<float> combBuffers[4];
        size_t combWritePos[4] = { 0, 0, 0, 0 };
        std::vector<float> allpassBuffers[2];
        size_t allpassWritePos[2] = { 0, 0 };
        float roomSize = 0.5f;
        float damping = 0.5f;
        float wetDry = 0.3f;
    };

    // Audio processor for effects
    struct AudioProcessor
    {
        ma_lpf lpf;
        ma_hpf hpf;
        ma_bpf bpf;
        ma_notch2 notch;
        ma_peak2 peak;
        ma_loshelf2 loshelf;
        ma_hishelf2 hishelf;
        EchoEffect echo;
        ReverbEffect reverb;
        AudioEffect activeEffect = AUDIO_EFFECT_NONE;
        bool enabled = false;
    };

    // Core Audio System Functions
    bool InitAudioDevice();
    bool InitAudioDeviceEx(const AudioConfig& config);
    void ShutdownAudioDevice();
    bool IsAudioDeviceReady();
    void SetMasterVolume(float volume);
    float GetMasterVolume();

    // Audio device management
    int GetAudioDeviceCount(ma_device_type type = ma_device_type_playback);
    std::string GetAudioDeviceName(int index, ma_device_type type = ma_device_type_playback);
    bool SetAudioDevice(int index);

    // Sound Loading/Unloading
    Sound LoadSound(const std::string& fileName);
    Sound LoadSoundFromWave(const void* data, unsigned int frameCount, unsigned int sampleRate, unsigned int channels, ma_format format = ma_format_f32);
    bool IsSoundReady(const Sound& sound);
    void UnloadSound(Sound& sound);

    // Sound Playback
    void PlaySound(const Sound& sound);
    void PlaySoundMulti(const Sound& sound);
    void StopSound(const Sound& sound);
    void PauseSound(const Sound& sound);
    void ResumeSound(const Sound& sound);
    bool IsSoundPlaying(const Sound& sound);

    // Sound Properties
    void SetSoundVolume(const Sound& sound, float volume);
    void SetSoundPitch(const Sound& sound, float pitch);
    void SetSoundPan(const Sound& sound, float pan);

    // Music Loading/Unloading
    Music LoadMusicStream(const std::string& fileName);
    bool IsMusicReady(const Music& music);
    void UnloadMusicStream(Music& music);

    // Music Playback
    void PlayMusicStream(Music& music);
    void StopMusicStream(Music& music);
    void PauseMusicStream(Music& music);
    void ResumeMusicStream(Music& music);
    void UpdateMusicStream(Music& music);
    bool IsMusicStreamPlaying(const Music& music);

    // Music Properties
    void SetMusicVolume(Music& music, float volume);
    void SetMusicPitch(Music& music, float pitch);
    void SetMusicPan(Music& music, float pan);
    void SetMusicLooping(Music& music, bool loop);
    float GetMusicTimeLength(const Music& music);
    float GetMusicTimePlayed(const Music& music);
    void SeekMusicStream(Music& music, float position);

    // Music Callbacks
    void SetMusicFinishedCallback(Music& music, std::function<void()> callback);

    // Audio Stream Functions
    AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int channels,
        ma_format format = ma_format_f32);
    void UnloadAudioStream(AudioStream& stream);
    void UpdateAudioStream(AudioStream& stream, const void* data, unsigned int frameCount);
    bool IsAudioStreamProcessed(const AudioStream& stream);
    void PlayAudioStream(AudioStream& stream);
    void PauseAudioStream(AudioStream& stream);
    void ResumeAudioStream(AudioStream& stream);
    bool IsAudioStreamPlaying(const AudioStream& stream);
    void StopAudioStream(AudioStream& stream);
    void SetAudioStreamVolume(AudioStream& stream, float volume);
    void SetAudioStreamPitch(AudioStream& stream, float pitch);
    void SetAudioStreamPan(AudioStream& stream, float pan);

    // 3D Audio Listener
    void SetAudioListenerPosition(float x, float y, float z);
    void SetAudioListenerDirection(float x, float y, float z);
    void SetAudioListenerVelocity(float x, float y, float z);
    void SetAudioListenerOrientation(float dirX, float dirY, float dirZ, float upX, float upY, float upZ);
    void SetAudioListener(const AudioListener& listener);
    AudioListener GetAudioListener();

    // 3D Audio Sound
    void SetSoundPosition(const Sound& sound, float x, float y, float z);
    void SetSoundVelocity(const Sound& sound, float x, float y, float z);
    void SetSoundDirection(const Sound& sound, float x, float y, float z);
    void SetSoundCone(const Sound& sound, float innerAngle, float outerAngle, float outerGain);
    void SetSoundAttenuation(const Sound& sound, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff);
    void SetSound3DConfig(const Sound& sound, const Audio3DConfig& config);
    void SetSoundSpatialization(const Sound& sound, bool enable);
    void SetSoundDopplerFactor(const Sound& sound, float factor);
    void SetSoundPositioning(const Sound& sound, ma_positioning mode);

    // 3D Audio Music
    void SetMusicPosition(Music& music, float x, float y, float z);
    void SetMusicVelocity(Music& music, float x, float y, float z);
    void SetMusicDirection(Music& music, float x, float y, float z);
    void SetMusicCone(Music& music, float innerAngle, float outerAngle, float outerGain);
    void SetMusicAttenuation(Music& music, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff);
    void SetMusic3DConfig(Music& music, const Audio3DConfig& config);
    void SetMusicSpatialization(Music& music, bool enable);
    void SetMusicDopplerFactor(Music& music, float factor);
    void SetMusicPositioning(Music& music, ma_positioning mode);

    // Audio Effects Processing
    void SetSoundEffect(const Sound& sound, AudioEffect effect, float param1 = 1000.0f, float param2 = 1.0f);
    void RemoveSoundEffect(const Sound& sound);
    void SetMusicEffect(Music& music, AudioEffect effect, float param1 = 1000.0f, float param2 = 1.0f);
    void RemoveMusicEffect(Music& music);

    // Audio Recording
    bool StartAudioRecording(unsigned int sampleRate = 44100, unsigned int channels = 2);
    void StopAudioRecording();
    bool IsRecordingAudio();
    std::vector<float> GetRecordedAudio();
    void SaveRecordedAudio(const std::string& fileName);

    // Waveform Generation
    Sound GenerateSoundWave(int waveType, float frequency, float duration, unsigned int sampleRate = 44100);
    void UpdateSoundWave(Sound& sound, int waveType, float frequency);

    // Audio Analysis
    float GetSoundVolume(const Sound& sound);
    float GetMusicVolume(const Music& music);
    std::vector<float> GetAudioSpectrumData(int sampleCount = 512);

    // Utility Functions
    std::string GetAudioFormatName(ma_format format);
    unsigned int GetAudioFormatSize(ma_format format);

    // Wave types for generation
    enum WaveType
    {
        WAVE_SINE = 0,
        WAVE_SQUARE,
        WAVE_TRIANGLE,
        WAVE_SAWTOOTH,
        WAVE_NOISE
    };
}