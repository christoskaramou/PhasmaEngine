#pragma once

#ifdef PE_AUDIO

namespace pe
{
    struct AudioSourceDesc
    {
        std::string filePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 50.0f;
        bool loop = false;
        bool spatial = true;
        bool autoplay = false;
    };
} // namespace pe

#endif // PE_AUDIO
