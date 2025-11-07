#pragma once

#define NOMINMAX
#include "miniaudio/include/miniaudio.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

// Todo: Change all the functions taking in a struct take in a reference

namespace cl {

    // Forward declarations
    struct Sound;
    struct Music;
    struct AudioStream;

    extern std::vector<Sound*> s_sounds; // Used for cl::Shutdown()
    extern std::vector<Music*> s_musics; // Used for cl::Shutdown()
    extern std::vector<AudioStream*> s_audioStreams; // Used for cl::Shutdown()

    // Audio configuration structure
    struct AudioConfig {
        unsigned int sampleRate = 48000;
        unsigned int channels = 2;
        ma_format format = ma_format_f32;
        unsigned int bufferSizeInFrames = 0; // 0 = auto
        ma_device_type deviceType = ma_device_type_playback;
    };

    // 3D Audio listener configuration
    struct AudioListener {
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
    struct Audio3DConfig {
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
    enum AudioEffect {
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
    struct Sound {
        ma_decoder decoder;
        ma_audio_buffer audioBuffer;
        float* pcmData = nullptr;
        bool isBuffer = false;
        bool valid = false;
        unsigned int frameCount = 0;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
    };

    // Music stream structure
    struct Music {
        ma_decoder decoder;
        ma_sound sound;
        bool valid = false;
        bool isPlaying = false;
        bool isPaused = false;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
        float volume = 1.0f;
        float pitch = 1.0f;
        float pan = 0.5f;
        bool looping = false;
        std::function<void()> onFinishCallback = nullptr;
    };

    // Audio stream for custom PCM data
    struct AudioStream {
        ma_pcm_rb buffer;
        ma_sound sound;
        ma_decoder decoder;
        bool valid = false;
        unsigned int sampleRate = 0;
        unsigned int channels = 0;
        ma_format format = ma_format_f32;
        unsigned int bufferSizeInFrames = 0;
    };

    // Audio processor for effects
    struct AudioProcessor {
        ma_lpf lpf;            // Low-pass filter
        ma_hpf hpf;            // High-pass filter
        ma_bpf bpf;            // Band-pass filter
        ma_notch2 notch;       // Notch filter
        ma_peak2 peak;         // Peaking filter
        ma_loshelf2 loshelf;   // Low shelf filter
        ma_hishelf2 hishelf;   // High shelf filter
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
    Sound LoadSound(std::string_view fileName);
    Sound LoadSoundFromWave(const void* data, unsigned int frameCount, unsigned int sampleRate, unsigned int channels, ma_format format = ma_format_f32);
    bool IsSoundReady(Sound sound);
    void UnloadSound(Sound sound);

    // Sound Playback
    void PlaySound(Sound sound);
    void PlaySoundMulti(Sound sound); // Play without stopping previous instances
    void StopSound(Sound sound);
    void PauseSound(Sound sound);
    void ResumeSound(Sound sound);
    bool IsSoundPlaying(Sound sound);

    // Sound Properties
    void SetSoundVolume(Sound sound, float volume);
    void SetSoundPitch(Sound sound, float pitch);
    void SetSoundPan(Sound sound, float pan);

    // Music Loading/Unloading
    Music LoadMusicStream(std::string_view fileName);
    bool IsMusicReady(Music music);
    void UnloadMusicStream(Music music);

    // Music Playback
    void PlayMusicStream(Music music);
    void StopMusicStream(Music music);
    void PauseMusicStream(Music music);
    void ResumeMusicStream(Music music);
    void UpdateMusicStream(Music music); // Update streaming buffer
    bool IsMusicStreamPlaying(Music music);

    // Music Properties
    void SetMusicVolume(Music music, float volume);
    void SetMusicPitch(Music music, float pitch);
    void SetMusicPan(Music music, float pan);
    void SetMusicLooping(Music music, bool loop);
    float GetMusicTimeLength(Music music);
    float GetMusicTimePlayed(Music music);
    void SeekMusicStream(Music music, float position);

    // Music Callbacks
    void SetMusicFinishedCallback(Music music, std::function<void()> callback);

    // Audio Stream Functions
    AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int channels, ma_format format = ma_format_f32);
    void UnloadAudioStream(AudioStream stream);
    void UpdateAudioStream(AudioStream stream, const void* data, unsigned int frameCount);
    bool IsAudioStreamProcessed(AudioStream stream);
    void PlayAudioStream(AudioStream stream);
    void PauseAudioStream(AudioStream stream);
    void ResumeAudioStream(AudioStream stream);
    bool IsAudioStreamPlaying(AudioStream stream);
    void StopAudioStream(AudioStream stream);
    void SetAudioStreamVolume(AudioStream stream, float volume);
    void SetAudioStreamPitch(AudioStream stream, float pitch);
    void SetAudioStreamPan(AudioStream stream, float pan);

    // 3D Audio Listener
    void SetAudioListenerPosition(float x, float y, float z);
    void SetAudioListenerDirection(float x, float y, float z);
    void SetAudioListenerVelocity(float x, float y, float z);
    void SetAudioListenerOrientation(float dirX, float dirY, float dirZ, float upX, float upY, float upZ);
    void SetAudioListener(const AudioListener& listener);
    AudioListener GetAudioListener();

    // 3D Audio Sound
    void SetSoundPosition(Sound sound, float x, float y, float z);
    void SetSoundVelocity(Sound sound, float x, float y, float z);
    void SetSoundDirection(Sound sound, float x, float y, float z);
    void SetSoundCone(Sound sound, float innerAngle, float outerAngle, float outerGain);
    void SetSoundAttenuation(Sound sound, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff);
    void SetSound3DConfig(Sound sound, const Audio3DConfig& config);
    void SetSoundSpatialization(Sound sound, bool enable);
    void SetSoundDopplerFactor(Sound sound, float factor);
    void SetSoundPositioning(Sound sound, ma_positioning mode);

    // 3D Audio Music
    void SetMusicPosition(Music music, float x, float y, float z);
    void SetMusicVelocity(Music music, float x, float y, float z);
    void SetMusicDirection(Music music, float x, float y, float z);
    void SetMusicCone(Music music, float innerAngle, float outerAngle, float outerGain);
    void SetMusicAttenuation(Music music, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff);
    void SetMusic3DConfig(Music music, const Audio3DConfig& config);
    void SetMusicSpatialization(Music music, bool enable);
    void SetMusicDopplerFactor(Music music, float factor);
    void SetMusicPositioning(Music music, ma_positioning mode);

    // Audio Effects Processing
    void SetSoundEffect(Sound sound, AudioEffect effect, float param1 = 1000.0f, float param2 = 1.0f);
    void RemoveSoundEffect(Sound sound);
    void SetMusicEffect(Music music, AudioEffect effect, float param1 = 1000.0f, float param2 = 1.0f);
    void RemoveMusicEffect(Music music);

    // Audio Recording
    bool StartAudioRecording(unsigned int sampleRate = 44100, unsigned int channels = 2);
    void StopAudioRecording();
    bool IsRecordingAudio();
    std::vector<float> GetRecordedAudio();
    void SaveRecordedAudio(std::string_view fileName);

    // Waveform Generation
    Sound GenerateSoundWave(int waveType, float frequency, float duration, unsigned int sampleRate = 44100);
    void UpdateSoundWave(Sound sound, int waveType, float frequency);

    // Audio Analysis
    float GetSoundVolume(Sound sound);
    float GetMusicVolume(Music music);
    std::vector<float> GetAudioSpectrumData(int sampleCount = 512);

    // Utility Functions
    std::string GetAudioFormatName(ma_format format);
    unsigned int GetAudioFormatSize(ma_format format);

    // Wave types for generation
    enum WaveType {
        WAVE_SINE = 0,
        WAVE_SQUARE,
        WAVE_TRIANGLE,
        WAVE_SAWTOOTH,
        WAVE_NOISE
    };

}