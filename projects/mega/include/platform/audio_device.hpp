// audio_device — 효과음 파형과 출력 장치(Win32 winmm waveOut).
//
// 계층 규칙(A3): games/ 는 오디오 장치 API 를 직접 열 수 없다. 이 코드는
// games/defense/audio.hpp 에 있었고 <windows.h> + <mmsystem.h> 를 게임 진입점으로
// 끌어들이던 세 경로 중 하나였다.
//
// WASAPI/XAudio2 는 COM 보일러플레이트가 크니, 클래식 PCM 재생 API `waveOut`(winmm)로 간단히.
//
// 이 파일은 둘로 나뉜다.
//
//   SfxTones     효과음 다섯 개의 파형. **OS 가 없어도 존재한다.** 합성은 순수
//                계산이고, 장치가 열리지 않았다는 이유로 그 계산이 다른 코드가
//                되면 헤드리스 검증이 제품과 다른 것을 재게 된다.
//   AudioDevice  스테레오 PCM 블록 하나를 장치에 밀어 넣는 일만 한다.
//
// 왜 장치가 스테레오 블록을 받는가. waveOut 에 여러 버퍼를 줄 세우면 그것들은
// **차례로** 재생된다 — 섞이지 않는다. 그래서 한 프레임에 난 소리 여럿을 각각
// 제출하면 총성이 겹치는 대신 줄을 서고, 화면과 소리가 어긋난다. 한 프레임의
// 보이스를 미리 하나로 합성해 제출하는 것이 그 문제의 해답이고, 합성은 게인이
// 정해지는 곳(카메라를 아는 층)에서 일어난다 — game/sfx_bus.hpp.
//
// 장치를 열지 않은 상태에서 재생 호출은 조용한 no-op 이다 — 헤드리스 실행이
// 오디오 때문에 다른 코드 경로를 타지 않게 하려는 것이다.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mega {
namespace platform {

enum class Sfx { Hit, Pickup, Death, Win, Step, Count };

// 효과음 파형. 장치와 무관하게 존재한다.
// snippet-begin: sfx-tones
class SfxTones {
public:
    static constexpr int kRate = 22050;

    SfxTones() {
        Synth(Sfx::Hit,    200.0f, 0.07f, /*square*/ true,  0.0f);
        Synth(Sfx::Pickup, 700.0f, 0.10f, false,            900.0f);   // 상승음
        Synth(Sfx::Death,  120.0f, 0.45f, true,            -60.0f);    // 하강·길게
        Synth(Sfx::Win,    523.0f, 0.30f, false,            400.0f);   // 밝게 상승
        Synth(Sfx::Step,   90.0f,  0.03f, true,             0.0f);     // 짧은 클릭
    }

    const float* Samples(Sfx sfx, size_t& count) const {
        if (sfx == Sfx::Count) { count = 0; return nullptr; }
        const std::vector<float>& buffer = buf_[static_cast<int>(sfx)];
        count = buffer.size();
        return buffer.data();
    }
    size_t LongestSamples() const {
        size_t longest = 0;
        for (int i = 0; i < static_cast<int>(Sfx::Count); ++i)
            if (buf_[i].size() > longest) longest = buf_[i].size();
        return longest;
    }

private:
    // freq(Hz)에서 시작, dur(초), square/sine, slide(끝까지 주파수 변화 Hz). 선형 감쇠 엔벨로프.
    void Synth(Sfx s, float freq, float dur, bool square, float slide) {
        const int n = static_cast<int>(kRate * dur);
        std::vector<float>& b = buf_[static_cast<int>(s)];
        b.resize(static_cast<size_t>(n > 0 ? n : 0));
        float phase = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float frac = static_cast<float>(i) / (n > 0 ? n : 1);
            const float fr = freq + slide * frac;
            phase += fr / kRate;
            const float v = square ? ((std::fmod(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f)
                                   : std::sin(6.2831853f * phase);
            b[static_cast<size_t>(i)] = v * (1.0f - frac);      // 선형 감쇠
        }
    }

    std::vector<float> buf_[static_cast<int>(Sfx::Count)];
};
// snippet-end: sfx-tones

}  // namespace platform
}  // namespace mega

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

namespace mega {
namespace platform {

class AudioDevice {
public:
    AudioDevice() = default;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    bool Init() {
        WAVEFORMATEX f{};
        f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = SfxTones::kRate;
        f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = SfxTones::kRate * 4;
        if (waveOutOpen(&hwo_, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
        open_ = true;
        return true;
    }
    bool Opened() const { return open_; }
    const SfxTones& Tones() const { return tones_; }

    // 이미 합성된 스테레오 블록(좌·우 교차 int16) 하나를 재생한다.
    void PlaySamples(const int16_t* interleaved, size_t frames) {
        if (!open_ || !interleaved || frames == 0) return;
        for (size_t slot = 0; slot < kPool; ++slot) {
            WAVEHDR& header = hdr_[slot];
            if (header.lpData != nullptr && !(header.dwFlags & WHDR_DONE)) continue;
            if (header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hwo_, &header, sizeof(header));
            std::vector<int16_t>& block = block_[slot];
            block.assign(interleaved, interleaved + frames * 2);
            ZeroMemory(&header, sizeof(header));
            header.lpData = reinterpret_cast<LPSTR>(block.data());
            header.dwBufferLength = static_cast<DWORD>(block.size() * sizeof(int16_t));
            if (waveOutPrepareHeader(hwo_, &header, sizeof(header)) == MMSYSERR_NOERROR)
                waveOutWrite(hwo_, &header, sizeof(header));
            return;
        }
    }

    // 위치 없는 소리(승패·배치 확인음). 좌우 같은 게인으로 중앙에 놓는다.
    void Play(Sfx s) {
        if (!open_ || s == Sfx::Count) return;
        size_t count = 0;
        const float* samples = tones_.Samples(s, count);
        if (!samples || count == 0) return;
        scratch_.resize(count * 2);
        for (size_t i = 0; i < count; ++i) {
            const int16_t v = static_cast<int16_t>(samples[i] * 8000.0f);
            scratch_[i * 2] = v;
            scratch_[i * 2 + 1] = v;
        }
        PlaySamples(scratch_.data(), count);
    }

    void Shutdown() {
        if (!open_) return;
        waveOutReset(hwo_);
        for (auto& h : hdr_) if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hwo_, &h, sizeof(h));
        waveOutClose(hwo_);
        hwo_ = nullptr; open_ = false;
    }
    ~AudioDevice() { Shutdown(); }

private:
    static const size_t kPool = 16;

    SfxTones             tones_;
    HWAVEOUT             hwo_ = nullptr;
    bool                 open_ = false;
    std::vector<int16_t> block_[kPool];
    std::vector<int16_t> scratch_;
    WAVEHDR              hdr_[kPool] = {};
};

}  // namespace platform
}  // namespace mega

#else  // 비-Windows: 파형은 그대로 있고 출력만 없다

namespace mega {
namespace platform {

class AudioDevice {
public:
    AudioDevice() = default;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    bool Init() { return false; }
    bool Opened() const { return false; }
    const SfxTones& Tones() const { return tones_; }
    void PlaySamples(const int16_t*, size_t) {}
    void Play(Sfx) {}
    void Shutdown() {}

private:
    SfxTones tones_;
};

}  // namespace platform
}  // namespace mega

#endif
