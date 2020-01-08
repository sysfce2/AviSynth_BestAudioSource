//  Copyright (c) 2020 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "audiosource.h"
#include <avisynth.h>
#include <string>
#include <memory>

template<typename T>
static void PackChannels(const uint8_t * const * const Src, uint8_t *Dst, size_t Length, size_t Channels) {
    const T * const * const S = reinterpret_cast<const T * const * const>(Src);
    T *D = reinterpret_cast<T *>(Dst);
    for (size_t i = 0; i < Length; i++) {
        for (size_t c = 0; c < Channels; c++)
            D[c] = S[c][i];
        D += Channels;
    }
}

class AvisynthAudioSource : public IClip {
    VideoInfo VI = {};
    std::unique_ptr<BestAudioSource> A;
public:
    AvisynthAudioSource(const char *SourceFile, int Track,
                        int AdjustDelay, bool ExactSamples, const char *VarPrefix, IScriptEnvironment* Env);
    bool __stdcall GetParity(int n) { return false; }
    int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; }
    const VideoInfo& __stdcall GetVideoInfo() { return VI; }
    void __stdcall GetAudio(void* Buf, __int64 Start, __int64 Count, IScriptEnvironment *Env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *Env) { return nullptr; };
};

AvisynthAudioSource::AvisynthAudioSource(const char *SourceFile, int Track,
                                         int AdjustDelay, bool ExactSamples, const char *VarPrefix, IScriptEnvironment *Env) {
    try {
        A.reset(new BestAudioSource(SourceFile, Track));
        if (ExactSamples)
            A->GetExactDuration();
    } catch (AudioException & E) {
        Env->ThrowError("BestAudioSource: %s", E.what());
    }

    const LWAudioProperties &AP = A->GetAudioProperties();
    VI.nchannels = AP.Channels;
    VI.num_audio_samples = AP.NumSamples;
    VI.audio_samples_per_second = AP.SampleRate;

    // casting to int should be safe; none of the channel constants are greater than INT_MAX
    Env->SetVar(Env->Sprintf("%s%s", VarPrefix, "BASCHANNEL_LAYOUT"), static_cast<int>(AP.ChannelLayout));

    Env->SetGlobalVar("BASVAR_PREFIX", VarPrefix);

    if (AP.IsFloat && AP.BytesPerSample == 4) {
        VI.sample_type = SAMPLE_FLOAT;
    } else if (!AP.IsFloat && AP.BytesPerSample == 1) {
        VI.sample_type = SAMPLE_INT8;
    } else if (!AP.IsFloat && AP.BytesPerSample == 2) {
        VI.sample_type = SAMPLE_INT16;
    } else if (!AP.IsFloat && AP.BytesPerSample == 4) {
        VI.sample_type = SAMPLE_INT32;
    } else {
        Env->ThrowError("BestAudioSource: Bad audio format");
    }
}

void AvisynthAudioSource::GetAudio(void* Buf, __int64 Start, __int64 Count, IScriptEnvironment *Env) {
    const LWAudioProperties &AP = A->GetAudioProperties();
    std::vector<uint8_t> Storage;
    Storage.resize(AP.Channels * Count * AP.BytesPerSample);
    std::vector<uint8_t *> Tmp;
    Tmp.reserve(AP.Channels);

    for (int i = 0; i < AP.Channels; i++)
        Tmp.push_back(Storage.data() + i * Count * AP.BytesPerSample);

    try {
        A->GetAudio(Tmp.data(), Start, Count);
    } catch (AudioException &E) {
        Env->ThrowError("BestAudioSource: %s", E.what());
    }

    if (AP.BytesPerSample == 1) {
        PackChannels<uint8_t>(Tmp.data(), reinterpret_cast<uint8_t *>(Buf), Count, AP.Channels);
    } else if (AP.BytesPerSample == 2) {
        PackChannels<uint16_t>(Tmp.data(), reinterpret_cast<uint8_t *>(Buf), Count, AP.Channels);
    } else if (AP.BytesPerSample == 4) {
        PackChannels<uint32_t>(Tmp.data(), reinterpret_cast<uint8_t *>(Buf), Count, AP.Channels);
    }
}

static AVSValue __cdecl CreateBestAudioSource(AVSValue Args, void* UserData, IScriptEnvironment* Env) {
    if (!Args[0].Defined())
        Env->ThrowError("BestAudioSource: No source specified");

    const char *Source = Args[0].AsString();
    int Track = Args[1].AsInt(-1);
    int AdjustDelay = Args[2].AsInt(-1);
    bool ExactSamples = Args[3].AsBool(false);
    const char *VarPrefix = Args[4].AsString("");

    return new AvisynthAudioSource(Source, Track, AdjustDelay, ExactSamples, VarPrefix, Env);
}

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* Env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    Env->AddFunction("BestAudioSource", "[source]s[track]i[adjustdelay]i[exactsamples]b[varprefix]s", CreateBestAudioSource, nullptr);
    return "BestAudioSource";
}