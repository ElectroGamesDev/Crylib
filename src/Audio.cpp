#include "Audio.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>

namespace cl {
    std::vector<Sound*> s_sounds;
    std::vector<Music*> s_musics;
    std::vector<AudioStream*> s_audioStreams;

    // Internal audio system state
    struct AudioSystem {
        ma_engine engine;
        ma_device device;
        ma_context context;
        ma_resource_manager resourceManager;
        ma_device_config deviceConfig;

        bool initialized = false;
        float masterVolume = 1.0f;
        AudioListener listener;

        // Recording
        ma_decoder recordingDecoder;
        ma_device recordingDevice;
        std::vector<float> recordingBuffer;
        bool isRecording = false;

        // Active sounds and music tracking
        std::unordered_map<ma_sound*, std::shared_ptr<ma_sound>> activeSounds;
        std::unordered_map<Music*, AudioProcessor> musicProcessors;
        std::unordered_map<Sound*, AudioProcessor> soundProcessors;

        // Device enumeration
        ma_device_info* playbackDeviceInfos = nullptr;
        ma_device_info* captureDeviceInfos = nullptr;
        ma_uint32 playbackDeviceCount = 0;
        ma_uint32 captureDeviceCount = 0;
    };

    static AudioSystem audioSystem;

    // Callback for data processing
    void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        ma_engine* pEngine = (ma_engine*)pDevice->pUserData;
        if (pEngine) {
            ma_engine_read_pcm_frames(pEngine, pOutput, frameCount, nullptr);
        }
    }

    // Recording callback
    void RecordingDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        if (audioSystem.isRecording && pInput) {
            float* samples = (float*)pInput;
            size_t sampleCount = frameCount * pDevice->capture.channels;
            audioSystem.recordingBuffer.insert(audioSystem.recordingBuffer.end(), samples, samples + sampleCount);
        }
    }

    // Core Audio System Functions
    bool InitAudioDevice() {
        AudioConfig config;
        return InitAudioDeviceEx(config);
    }

    bool InitAudioDeviceEx(const AudioConfig& config) {
        if (audioSystem.initialized) {
            return true;
        }

        ma_result result;

        // Initialize context for device enumeration
        result = ma_context_init(nullptr, 0, nullptr, &audioSystem.context);
        if (result != MA_SUCCESS) {
            return false;
        }

        // Enumerate devices
        result = ma_context_get_devices(&audioSystem.context,
            &audioSystem.playbackDeviceInfos,
            &audioSystem.playbackDeviceCount,
            &audioSystem.captureDeviceInfos,
            &audioSystem.captureDeviceCount);

        // Initialize engine configuration
        ma_engine_config engineConfig = ma_engine_config_init();
        engineConfig.sampleRate = config.sampleRate;
        engineConfig.channels = config.channels;
        engineConfig.periodSizeInFrames = config.bufferSizeInFrames;
        engineConfig.noAutoStart = MA_FALSE;

        // Initialize the engine
        result = ma_engine_init(&engineConfig, &audioSystem.engine);
        if (result != MA_SUCCESS) {
            ma_context_uninit(&audioSystem.context);
            return false;
        }

        // Set default listener orientation
        ma_engine_listener_set_position(&audioSystem.engine, 0, 0.0f, 0.0f, 0.0f);
        ma_engine_listener_set_direction(&audioSystem.engine, 0, 0.0f, 0.0f, -1.0f);
        ma_engine_listener_set_world_up(&audioSystem.engine, 0, 0.0f, 1.0f, 0.0f);

        audioSystem.initialized = true;
        audioSystem.masterVolume = 1.0f;

        return true;
    }

    void ShutdownAudioDevice() {
        if (!audioSystem.initialized) {
            return;
        }

        // Stop all active sounds
        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_uninit(pair.second.get());
        }
        audioSystem.activeSounds.clear();

        // Clean up processors
        audioSystem.musicProcessors.clear();
        audioSystem.soundProcessors.clear();

        // Stop recording if active
        if (audioSystem.isRecording) {
            StopAudioRecording();
        }

        // Uninitialize engine and context
        ma_engine_uninit(&audioSystem.engine);
        ma_context_uninit(&audioSystem.context);

        audioSystem.initialized = false;
    }

    bool IsAudioDeviceReady() {
        return audioSystem.initialized;
    }

    void SetMasterVolume(float volume) {
        if (!audioSystem.initialized) return;
        audioSystem.masterVolume = std::clamp(volume, 0.0f, 1.0f);
        ma_engine_set_volume(&audioSystem.engine, audioSystem.masterVolume);
    }

    float GetMasterVolume() {
        return audioSystem.masterVolume;
    }

    // Audio device management
    int GetAudioDeviceCount(ma_device_type type) {
        if (!audioSystem.initialized) return 0;

        if (type == ma_device_type_playback) {
            return (int)audioSystem.playbackDeviceCount;
        }
        else {
            return (int)audioSystem.captureDeviceCount;
        }
    }

    std::string GetAudioDeviceName(int index, ma_device_type type) {
        if (!audioSystem.initialized) return nullptr;

        if (type == ma_device_type_playback) {
            if (index >= 0 && index < (int)audioSystem.playbackDeviceCount) {
                return audioSystem.playbackDeviceInfos[index].name;
            }
        }
        else {
            if (index >= 0 && index < (int)audioSystem.captureDeviceCount) {
                return audioSystem.captureDeviceInfos[index].name;
            }
        }

        return nullptr;
    }

    bool SetAudioDevice(int index) {
        // Todo: Add this. Changing device requires reinitialization in miniaudio
        return false;
    }

    // Sound Loading/Unloading
    Sound LoadSound(std::string_view fileName) {
        Sound sound = {};

        if (!audioSystem.initialized || !fileName.data()) return sound;

        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_decoder decoder;
        if (ma_decoder_init_file(fileName.data(), &decoderConfig, &decoder) != MA_SUCCESS) return sound;

        ma_uint64 frameCount;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS) {
            ma_decoder_uninit(&decoder);
            return sound;
        }

        sound.frameCount = static_cast<unsigned int>(frameCount);
        sound.channels = decoder.outputChannels;
        sound.sampleRate = decoder.outputSampleRate;

        // Allocate PCM memory that lives beyond this function
        sound.pcmData = new float[frameCount * sound.channels];

        ma_uint64 framesRead;
        if (ma_decoder_read_pcm_frames(&decoder, sound.pcmData, frameCount, &framesRead) != MA_SUCCESS || framesRead != frameCount) {
            ma_decoder_uninit(&decoder);
            delete[] sound.pcmData;
            return sound;
        }

        ma_decoder_uninit(&decoder);

        // Init the audio buffer with the data pointer
        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            ma_format_f32,
            sound.channels,
            frameCount,
            sound.pcmData,
            nullptr
        );

        if (ma_audio_buffer_init(&bufferConfig, &sound.audioBuffer) != MA_SUCCESS) {
            delete[] sound.pcmData;
            return sound;
        }

        sound.isBuffer = true;
        sound.valid = true;

        s_sounds.push_back(&sound);

        return sound;
    }


    Sound LoadSoundFromWave(const void* data, unsigned int frameCount, unsigned int sampleRate, unsigned int channels, ma_format format) {
        Sound sound = { 0 };

        if (!audioSystem.initialized || !data) {
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
        if (result != MA_SUCCESS) {
            return sound;
        }

        sound.isBuffer = true;
        sound.valid = true;
        sound.frameCount = frameCount;
        sound.sampleRate = sampleRate;
        sound.channels = channels;

        s_sounds.push_back(&sound);

        return sound;
    }

    bool IsSoundReady(Sound sound) {
        return sound.valid;
    }

    void UnloadSound(Sound sound) {
        if (!sound.valid) return;

        if (sound.isBuffer) {
            ma_audio_buffer_uninit(&sound.audioBuffer);
        }
        else {
            ma_decoder_uninit(&sound.decoder);
        }

        for (size_t i = 0; i < s_sounds.size(); ++i)
        {
            if (s_sounds[i] == &sound)
            {
                if (i != s_sounds.size() - 1)
                    std::swap(s_sounds[i], s_sounds.back());

                s_sounds.pop_back();
                return;
            }
        }
    }

    // Sound Playback
    void PlaySound(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return;

        auto maSound = std::make_shared<ma_sound>();

        ma_result result = ma_sound_init_from_data_source(&audioSystem.engine, &sound.audioBuffer,
            MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
            nullptr, maSound.get());

        if (result == MA_SUCCESS) {
            ma_sound_start(maSound.get());
            audioSystem.activeSounds[maSound.get()] = maSound;
        }
    }

    void PlaySoundMulti(Sound sound) {
        PlaySound(sound); // In this implementation, both work the same
    }

    void StopSound(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return;

        // Stop all instances of this sound
        auto it = audioSystem.activeSounds.begin();
        while (it != audioSystem.activeSounds.end()) {
            ma_sound_stop(it->second.get());
            it = audioSystem.activeSounds.erase(it);
        }
    }

    void PauseSound(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            if (ma_sound_is_playing(pair.second.get())) {
                ma_sound_stop(pair.second.get());
            }
        }
    }

    void ResumeSound(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_start(pair.second.get());
        }
    }

    bool IsSoundPlaying(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return false;

        for (auto& pair : audioSystem.activeSounds) {
            if (ma_sound_is_playing(pair.second.get())) {
                return true;
            }
        }
        return false;
    }

    // Sound Properties
    void SetSoundVolume(Sound sound, float volume) {
        if (!audioSystem.initialized || !sound.valid) return;

        volume = std::clamp(volume, 0.0f, 1.0f);
        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_volume(pair.second.get(), volume);
        }
    }

    void SetSoundPitch(Sound sound, float pitch) {
        if (!audioSystem.initialized || !sound.valid) return;

        pitch = std::max(0.1f, pitch);
        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_pitch(pair.second.get(), pitch);
        }
    }

    void SetSoundPan(Sound sound, float pan) {
        if (!audioSystem.initialized || !sound.valid) return;

        pan = std::clamp(pan, 0.0f, 1.0f);
        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_pan(pair.second.get(), pan);
        }
    }

    // Music Loading/Unloading
    Music LoadMusicStream(std::string_view fileName) {
        Music music = { 0 };

        if (!audioSystem.initialized || !fileName.data()) {
            return music;
        }

        ma_result result = ma_sound_init_from_file(&audioSystem.engine, fileName.data(),
            MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
            nullptr, nullptr, &music.sound);

        if (result != MA_SUCCESS) {
            return music;
        }

        ma_sound_get_data_source(&music.sound);

        music.valid = true;
        music.volume = 1.0f;
        music.pitch = 1.0f;
        music.pan = 0.5f;
        music.looping = false;

        s_musics.push_back(&music);

        return music;
    }

    bool IsMusicReady(Music music) {
        return music.valid;
    }

    void UnloadMusicStream(Music music) {
        if (!music.valid) return;

        if (music.isPlaying) {
            ma_sound_stop(&music.sound);
        }

        ma_sound_uninit(&music.sound);

        // Remove processor if exists
        audioSystem.musicProcessors.erase(&music);

        for (size_t i = 0; i < s_musics.size(); ++i)
        {
            if (s_musics[i] == &music)
            {
                if (i != s_musics.size() - 1)
                    std::swap(s_musics[i], s_musics.back());

                s_musics.pop_back();
                return;
            }
        }
    }

    // Music Playback
    void PlayMusicStream(Music music) {
        if (!audioSystem.initialized || !music.valid) return;

        ma_sound_start(&music.sound);
        music.isPlaying = true;
        music.isPaused = false;
    }

    void StopMusicStream(Music music) {
        if (!audioSystem.initialized || !music.valid) return;

        ma_sound_stop(&music.sound);
        ma_sound_seek_to_pcm_frame(&music.sound, 0);
        music.isPlaying = false;
        music.isPaused = false;
    }

    void PauseMusicStream(Music music) {
        if (!audioSystem.initialized || !music.valid) return;

        if (music.isPlaying && !music.isPaused) {
            ma_sound_stop(&music.sound);
            music.isPaused = true;
        }
    }

    void ResumeMusicStream(Music music) {
        if (!audioSystem.initialized || !music.valid) return;

        if (music.isPaused) {
            ma_sound_start(&music.sound);
            music.isPaused = false;
        }
    }

    void UpdateMusicStream(Music music) {
        if (!audioSystem.initialized || !music.valid) return;

        // Check if music finished
        if (music.isPlaying && !ma_sound_is_playing(&music.sound)) {
            if (music.looping) {
                ma_sound_seek_to_pcm_frame(&music.sound, 0);
                ma_sound_start(&music.sound);
            }
            else {
                music.isPlaying = false;
                if (music.onFinishCallback) {
                    music.onFinishCallback();
                }
            }
        }
    }

    bool IsMusicStreamPlaying(Music music) {
        if (!audioSystem.initialized || !music.valid) return false;
        return ma_sound_is_playing(&music.sound);
    }

    // Music Properties
    void SetMusicVolume(Music music, float volume) {
        if (!audioSystem.initialized || !music.valid) return;

        volume = std::clamp(volume, 0.0f, 1.0f);
        music.volume = volume;
        ma_sound_set_volume(&music.sound, volume);
    }

    void SetMusicPitch(Music music, float pitch) {
        if (!audioSystem.initialized || !music.valid) return;

        pitch = std::max(0.1f, pitch);
        music.pitch = pitch;
        ma_sound_set_pitch(&music.sound, pitch);
    }

    void SetMusicPan(Music music, float pan) {
        if (!audioSystem.initialized || !music.valid) return;

        pan = std::clamp(pan, 0.0f, 1.0f);
        music.pan = pan;
        ma_sound_set_pan(&music.sound, pan);
    }

    void SetMusicLooping(Music music, bool loop) {
        if (!audioSystem.initialized || !music.valid) return;

        music.looping = loop;
        ma_sound_set_looping(&music.sound, loop ? MA_TRUE : MA_FALSE);
    }

    float GetMusicTimeLength(Music music) {
        if (!audioSystem.initialized || !music.valid) return 0.0f;

        ma_uint64 lengthInFrames;
        ma_sound_get_length_in_pcm_frames(&music.sound, &lengthInFrames);

        ma_uint32 sampleRate;
        ma_sound_get_data_format(&music.sound, nullptr, nullptr, &sampleRate, nullptr, 0);

        return (float)lengthInFrames / (float)sampleRate;
    }

    float GetMusicTimePlayed(Music music) {
        if (!audioSystem.initialized || !music.valid) return 0.0f;

        ma_uint64 cursorInFrames;
        ma_sound_get_cursor_in_pcm_frames(&music.sound, &cursorInFrames);

        ma_uint32 sampleRate;
        ma_sound_get_data_format(&music.sound, nullptr, nullptr, &sampleRate, nullptr, 0);

        return (float)cursorInFrames / (float)sampleRate;
    }

    void SeekMusicStream(Music music, float position) {
        if (!audioSystem.initialized || !music.valid) return;

        ma_uint32 sampleRate;
        ma_sound_get_data_format(&music.sound, nullptr, nullptr, &sampleRate, nullptr, 0);

        ma_uint64 framePosition = (ma_uint64)(position * sampleRate);
        ma_sound_seek_to_pcm_frame(&music.sound, framePosition);
    }

    void SetMusicFinishedCallback(Music music, std::function<void()> callback) {
        if (!music.valid) return;
        music.onFinishCallback = callback;
    }

    // Audio Stream Functions
    AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int channels, ma_format format) {
        AudioStream stream = { 0 };

        if (!audioSystem.initialized) {
            return stream;
        }

        stream.sampleRate = sampleRate;
        stream.channels = channels;
        stream.format = format;
        stream.bufferSizeInFrames = sampleRate; // 1 second buffer

        // Initialize ring buffer
        ma_uint32 subbufferSizeInFrames = stream.bufferSizeInFrames / 3;
        ma_result result = ma_pcm_rb_init(format, channels, subbufferSizeInFrames, nullptr, nullptr, &stream.buffer);

        if (result != MA_SUCCESS) {
            return stream;
        }

        stream.valid = true;

        s_audioStreams.push_back(&stream);

        return stream;
    }

    void UnloadAudioStream(AudioStream stream) {
        if (!stream.valid) return;

        ma_pcm_rb_uninit(&stream.buffer);

        for (size_t i = 0; i < s_audioStreams.size(); ++i)
        {
            if (s_audioStreams[i] == &stream)
            {
                if (i != s_audioStreams.size() - 1)
                    std::swap(s_audioStreams[i], s_audioStreams.back());

                s_audioStreams.pop_back();
                return;
            }
        }
    }

    void UpdateAudioStream(AudioStream stream, const void* data, unsigned int frameCount) {
        if (!audioSystem.initialized || !stream.valid || !data) return;

        void* mappedBuffer;
        ma_pcm_rb_acquire_write(&stream.buffer, &frameCount, &mappedBuffer);

        if (mappedBuffer != nullptr && frameCount > 0) {
            size_t bytesPerFrame = ma_get_bytes_per_frame(stream.format, stream.channels);
            memcpy(mappedBuffer, data, frameCount * bytesPerFrame);
            ma_pcm_rb_commit_write(&stream.buffer, frameCount);
        }
    }

    bool IsAudioStreamProcessed(AudioStream stream) {
        if (!stream.valid) return false;

        ma_uint32 availableFrames = ma_pcm_rb_available_write(&stream.buffer);
        return availableFrames > (stream.bufferSizeInFrames / 2);
    }

    void PlayAudioStream(AudioStream stream) {
        if (!audioSystem.initialized || !stream.valid) return;
        ma_sound_start(&stream.sound);
    }

    void PauseAudioStream(AudioStream stream) {
        if (!audioSystem.initialized || !stream.valid) return;
        ma_sound_stop(&stream.sound);
    }

    void ResumeAudioStream(AudioStream stream) {
        PlayAudioStream(stream);
    }

    bool IsAudioStreamPlaying(AudioStream stream) {
        if (!audioSystem.initialized || !stream.valid) return false;
        return ma_sound_is_playing(&stream.sound);
    }

    void StopAudioStream(AudioStream stream) {
        if (!audioSystem.initialized || !stream.valid) return;
        ma_sound_stop(&stream.sound);
    }

    void SetAudioStreamVolume(AudioStream stream, float volume) {
        if (!audioSystem.initialized || !stream.valid) return;
        volume = std::clamp(volume, 0.0f, 1.0f);
        ma_sound_set_volume(&stream.sound, volume);
    }

    void SetAudioStreamPitch(AudioStream stream, float pitch) {
        if (!audioSystem.initialized || !stream.valid) return;
        pitch = std::max(0.1f, pitch);
        ma_sound_set_pitch(&stream.sound, pitch);
    }

    void SetAudioStreamPan(AudioStream stream, float pan) {
        if (!audioSystem.initialized || !stream.valid) return;
        pan = std::clamp(pan, 0.0f, 1.0f);
        ma_sound_set_pan(&stream.sound, pan);
    }

    // 3D Audio - Listener
    void SetAudioListenerPosition(float x, float y, float z) {
        if (!audioSystem.initialized) return;

        audioSystem.listener.positionX = x;
        audioSystem.listener.positionY = y;
        audioSystem.listener.positionZ = z;

        ma_engine_listener_set_position(&audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerDirection(float x, float y, float z) {
        if (!audioSystem.initialized) return;

        audioSystem.listener.directionX = x;
        audioSystem.listener.directionY = y;
        audioSystem.listener.directionZ = z;

        ma_engine_listener_set_direction(&audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerVelocity(float x, float y, float z) {
        if (!audioSystem.initialized) return;

        audioSystem.listener.velocityX = x;
        audioSystem.listener.velocityY = y;
        audioSystem.listener.velocityZ = z;

        ma_engine_listener_set_velocity(&audioSystem.engine, 0, x, y, z);
    }

    void SetAudioListenerOrientation(float dirX, float dirY, float dirZ, float upX, float upY, float upZ) {
        if (!audioSystem.initialized) return;

        SetAudioListenerDirection(dirX, dirY, dirZ);

        audioSystem.listener.worldUpX = upX;
        audioSystem.listener.worldUpY = upY;
        audioSystem.listener.worldUpZ = upZ;

        ma_engine_listener_set_world_up(&audioSystem.engine, 0, upX, upY, upZ);
    }

    void SetAudioListener(const AudioListener& listener) {
        audioSystem.listener = listener;
        SetAudioListenerPosition(listener.positionX, listener.positionY, listener.positionZ);
        SetAudioListenerDirection(listener.directionX, listener.directionY, listener.directionZ);
        SetAudioListenerVelocity(listener.velocityX, listener.velocityY, listener.velocityZ);
        SetAudioListenerOrientation(listener.directionX, listener.directionY, listener.directionZ,
            listener.worldUpX, listener.worldUpY, listener.worldUpZ);
    }

    AudioListener GetAudioListener() {
        return audioSystem.listener;
    }

    // 3D Audio - Sound
    void SetSoundPosition(Sound sound, float x, float y, float z) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_position(pair.second.get(), x, y, z);
        }
    }

    void SetSoundVelocity(Sound sound, float x, float y, float z) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_velocity(pair.second.get(), x, y, z);
        }
    }

    void SetSoundDirection(Sound sound, float x, float y, float z) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_direction(pair.second.get(), x, y, z);
        }
    }

    void SetSoundCone(Sound sound, float innerAngle, float outerAngle, float outerGain) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_cone(pair.second.get(), innerAngle, outerAngle, outerGain);
        }
    }

    void SetSoundAttenuation(Sound sound, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_attenuation_model(pair.second.get(), model);
            ma_sound_set_min_distance(pair.second.get(), minDistance);
            ma_sound_set_max_distance(pair.second.get(), maxDistance);
            ma_sound_set_rolloff(pair.second.get(), rolloff);
        }
    }

    void SetSound3DConfig(Sound sound, const Audio3DConfig& config) {
        SetSoundPosition(sound, config.positionX, config.positionY, config.positionZ);
        SetSoundVelocity(sound, config.velocityX, config.velocityY, config.velocityZ);
        SetSoundDirection(sound, config.directionX, config.directionY, config.directionZ);
        SetSoundCone(sound, config.coneInnerAngle, config.coneOuterAngle, config.coneOuterGain);
        SetSoundAttenuation(sound, config.attenuationModel, config.minDistance, config.maxDistance, config.rolloff);
        SetSoundDopplerFactor(sound, config.dopplerFactor);
        SetSoundPositioning(sound, config.positioning);
    }

    void SetSoundSpatialization(Sound sound, bool enable) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_spatialization_enabled(pair.second.get(), enable ? MA_TRUE : MA_FALSE);
        }
    }

    void SetSoundDopplerFactor(Sound sound, float factor) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_doppler_factor(pair.second.get(), factor);
        }
    }

    void SetSoundPositioning(Sound sound, ma_positioning mode) {
        if (!audioSystem.initialized || !sound.valid) return;

        for (auto& pair : audioSystem.activeSounds) {
            ma_sound_set_positioning(pair.second.get(), mode);
        }
    }

    // 3D Audio - Music
    void SetMusicPosition(Music music, float x, float y, float z) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_position(&music.sound, x, y, z);
    }

    void SetMusicVelocity(Music music, float x, float y, float z) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_velocity(&music.sound, x, y, z);
    }

    void SetMusicDirection(Music music, float x, float y, float z) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_direction(&music.sound, x, y, z);
    }

    void SetMusicCone(Music music, float innerAngle, float outerAngle, float outerGain) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_cone(&music.sound, innerAngle, outerAngle, outerGain);
    }

    void SetMusicAttenuation(Music music, ma_attenuation_model model, float minDistance, float maxDistance, float rolloff) {
        if (!audioSystem.initialized || !music.valid) return;

        ma_sound_set_attenuation_model(&music.sound, model);
        ma_sound_set_min_distance(&music.sound, minDistance);
        ma_sound_set_max_distance(&music.sound, maxDistance);
        ma_sound_set_rolloff(&music.sound, rolloff);
    }

    void SetMusic3DConfig(Music music, const Audio3DConfig& config) {
        SetMusicPosition(music, config.positionX, config.positionY, config.positionZ);
        SetMusicVelocity(music, config.velocityX, config.velocityY, config.velocityZ);
        SetMusicDirection(music, config.directionX, config.directionY, config.directionZ);
        SetMusicCone(music, config.coneInnerAngle, config.coneOuterAngle, config.coneOuterGain);
        SetMusicAttenuation(music, config.attenuationModel, config.minDistance, config.maxDistance, config.rolloff);
        SetMusicDopplerFactor(music, config.dopplerFactor);
        SetMusicPositioning(music, config.positioning);
    }

    void SetMusicSpatialization(Music music, bool enable) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_spatialization_enabled(&music.sound, enable ? MA_TRUE : MA_FALSE);
    }

    void SetMusicDopplerFactor(Music music, float factor) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_doppler_factor(&music.sound, factor);
    }

    void SetMusicPositioning(Music music, ma_positioning mode) {
        if (!audioSystem.initialized || !music.valid) return;
        ma_sound_set_positioning(&music.sound, mode);
    }

    // Audio Effects Processing
    void SetSoundEffect(Sound sound, AudioEffect effect, float param1, float param2) {
        if (!audioSystem.initialized || !sound.valid) return;
        // Todo: Add advanced effect processing using custom DSP nodes
        AudioProcessor& processor = audioSystem.soundProcessors[&sound];
        processor.activeEffect = effect;
        processor.enabled = true;
        ma_format format = ma_format_f32;
        ma_uint32 sampleRate = sound.sampleRate;
        ma_uint32 channels = sound.channels;
        switch (effect) {
            case AUDIO_EFFECT_LOWPASS:
            {
                ma_lpf_config config = ma_lpf_config_init(format, channels, sampleRate, param1, 2);
                ma_lpf_init(&config, nullptr, &processor.lpf);
            }
            break;
            case AUDIO_EFFECT_HIGHPASS:
            {
                ma_hpf_config config = ma_hpf_config_init(format, channels, sampleRate, param1, 2);
                ma_hpf_init(&config, nullptr, &processor.hpf);
            }
            break;
            case AUDIO_EFFECT_BANDPASS:
            {
                ma_bpf_config config = ma_bpf_config_init(format, channels, sampleRate, param1, 2);
                ma_bpf_init(&config, nullptr, &processor.bpf);
            }
            break;
            case AUDIO_EFFECT_NOTCH:
            {
                ma_notch2_config config = ma_notch2_config_init(format, channels, sampleRate, param1, param2);
                ma_notch2_init(&config, nullptr, &processor.notch);
            }
            break;
            case AUDIO_EFFECT_PEAKING:
            {
                ma_peak2_config config = ma_peak2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_peak2_init(&config, nullptr, &processor.peak);
            }
            break;
            case AUDIO_EFFECT_LOSHELF:
            {
                ma_loshelf2_config config = ma_loshelf2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_loshelf2_init(&config, nullptr, &processor.loshelf);
            }
            break;
            case AUDIO_EFFECT_HISHELF:
            {
                ma_hishelf2_config config = ma_hishelf2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_hishelf2_init(&config, nullptr, &processor.hishelf);
            }
            break;
            default:
                processor.enabled = false;
                break;
        }
    }

    void RemoveSoundEffect(Sound sound) {
        if (!sound.valid) return;

        auto it = audioSystem.soundProcessors.find(&sound);
        if (it != audioSystem.soundProcessors.end()) {
            audioSystem.soundProcessors.erase(it);
        }
    }

    void SetMusicEffect(Music music, AudioEffect effect, float param1, float param2) {
        if (!audioSystem.initialized || !music.valid) return;

        AudioProcessor& processor = audioSystem.musicProcessors[&music];
        processor.activeEffect = effect;
        processor.enabled = true;

        ma_format format;
        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_sound_get_data_format(&music.sound, &format, &channels, &sampleRate, nullptr, 0);

        switch (effect) {
            case AUDIO_EFFECT_LOWPASS:
            {
                ma_lpf_config config = ma_lpf_config_init(format, channels, sampleRate, param1, 2);
                ma_lpf_init(&config, nullptr, &processor.lpf);
            }
            break;
            case AUDIO_EFFECT_HIGHPASS:
            {
                ma_hpf_config config = ma_hpf_config_init(format, channels, sampleRate, param1, 2);
                ma_hpf_init(&config, nullptr, &processor.hpf);
            }
            break;
            case AUDIO_EFFECT_BANDPASS:
            {
                ma_bpf_config config = ma_bpf_config_init(format, channels, sampleRate, param1, 2);
                ma_bpf_init(&config, nullptr, &processor.bpf);
            }
            break;
            case AUDIO_EFFECT_NOTCH:
            {
                ma_notch2_config config = ma_notch2_config_init(format, channels, sampleRate, param1, param2);
                ma_notch2_init(&config, nullptr, &processor.notch);
            }
            break;
            case AUDIO_EFFECT_PEAKING:
            {
                ma_peak2_config config = ma_peak2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_peak2_init(&config, nullptr, &processor.peak);
            }
            break;
            case AUDIO_EFFECT_LOSHELF:
            {
                ma_loshelf2_config config = ma_loshelf2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_loshelf2_init(&config, nullptr, &processor.loshelf);
            }
            break;
            case AUDIO_EFFECT_HISHELF:
            {
                ma_hishelf2_config config = ma_hishelf2_config_init(format, channels, sampleRate, param1, 0.707, param2);
                ma_hishelf2_init(&config, nullptr, &processor.hishelf);
            }
            break;
            default:
                processor.enabled = false;
                break;
        }
    }

    void RemoveMusicEffect(Music music) {
        if (!music.valid) return;

        auto it = audioSystem.musicProcessors.find(&music);
        if (it != audioSystem.musicProcessors.end()) {
            audioSystem.musicProcessors.erase(it);
        }
    }

    // Audio Recording
    bool StartAudioRecording(unsigned int sampleRate, unsigned int channels) {
        if (!audioSystem.initialized || audioSystem.isRecording) {
            return false;
        }

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
        deviceConfig.capture.format = ma_format_f32;
        deviceConfig.capture.channels = channels;
        deviceConfig.sampleRate = sampleRate;
        deviceConfig.dataCallback = RecordingDataCallback;
        deviceConfig.pUserData = nullptr;

        ma_result result = ma_device_init(&audioSystem.context, &deviceConfig, &audioSystem.recordingDevice);
        if (result != MA_SUCCESS) {
            return false;
        }

        result = ma_device_start(&audioSystem.recordingDevice);
        if (result != MA_SUCCESS) {
            ma_device_uninit(&audioSystem.recordingDevice);
            return false;
        }

        audioSystem.isRecording = true;
        audioSystem.recordingBuffer.clear();

        return true;
    }

    void StopAudioRecording() {
        if (!audioSystem.isRecording) return;

        ma_device_stop(&audioSystem.recordingDevice);
        ma_device_uninit(&audioSystem.recordingDevice);
        audioSystem.isRecording = false;
    }

    bool IsRecordingAudio() {
        return audioSystem.isRecording;
    }

    std::vector<float> GetRecordedAudio() {
        return audioSystem.recordingBuffer;
    }

    void SaveRecordedAudio(std::string_view fileName) {
        if (audioSystem.recordingBuffer.empty() || !fileName.data()) return;

        ma_encoder_config config = ma_encoder_config_init(
            ma_encoding_format_wav,
            ma_format_f32,
            audioSystem.recordingDevice.capture.channels,
            audioSystem.recordingDevice.sampleRate
        );

        ma_encoder encoder;
        ma_result result = ma_encoder_init_file(fileName.data(), &config, &encoder);
        if (result != MA_SUCCESS) return;

        ma_uint64 framesToWrite = audioSystem.recordingBuffer.size() / audioSystem.recordingDevice.capture.channels;
        ma_uint64 framesWritten = 0;

        result = ma_encoder_write_pcm_frames(&encoder,
            audioSystem.recordingBuffer.data(),
            framesToWrite,
            &framesWritten);

        if (result != MA_SUCCESS) {
            // Todo: handle error
        }

        ma_encoder_uninit(&encoder);
    }

    // Waveform Generation
    Sound GenerateSoundWave(int waveType, float frequency, float duration, unsigned int sampleRate) {
        if (!audioSystem.initialized) {
            return Sound{ 0 };
        }

        unsigned int frameCount = (unsigned int)(duration * sampleRate);
        std::vector<float> samples(frameCount);

        float period = sampleRate / frequency;

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (unsigned int i = 0; i < frameCount; i++) {
            float t = (float)i / period;

            switch (waveType) {
            case WAVE_SINE:
                samples[i] = sinf(2.0f * 3.14159265f * t);
                break;
            case WAVE_SQUARE:
                samples[i] = (sinf(2.0f * 3.14159265f * t) >= 0.0f) ? 1.0f : -1.0f;
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

        return LoadSoundFromWave(samples.data(), frameCount, sampleRate, 1, ma_format_f32);
    }

    void UpdateSoundWave(Sound sound, int waveType, float frequency) {
        // Todo: Implement this. We would need to regenerate the sound buffer
    }

    // Audio Analysis
    float GetSoundVolume(Sound sound) {
        if (!audioSystem.initialized || !sound.valid) return 0.0f;

        // Return the set volume from active sounds
        for (auto& pair : audioSystem.activeSounds) {
            return ma_sound_get_volume(pair.second.get());
        }

        return 0.0f;
    }

    float GetMusicVolume(Music music) {
        if (!audioSystem.initialized || !music.valid) return 0.0f;
        return music.volume;
    }

    std::vector<float> GetAudioSpectrumData(int sampleCount) {
        // Todo: Implement this. FFT spectrum analysis would require additional DSP implementation
        return std::vector<float>(sampleCount, 0.0f);
    }

    // Utility Functions
    std::string GetAudioFormatName(ma_format format) {
        switch (format) {
            case ma_format_u8: return "8-bit unsigned";
            case ma_format_s16: return "16-bit signed";
            case ma_format_s24: return "24-bit signed";
            case ma_format_s32: return "32-bit signed";
            case ma_format_f32: return "32-bit float";
            default: return "Unknown";
        }
    }

    unsigned int GetAudioFormatSize(ma_format format) {
        return ma_get_bytes_per_sample(format);
    }

}