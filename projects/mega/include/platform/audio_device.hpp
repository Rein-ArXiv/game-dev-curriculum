// audio_device — 효과음 출력 장치(Win32 winmm waveOut).
//
// 계층 규칙(A3): games/ 는 오디오 장치 API 를 직접 열 수 없다. 이 코드는
// games/defense/audio.hpp 에 있었고 <windows.h> + <mmsystem.h> 를 게임 진입점으로
// 끌어들이던 세 경로 중 하나였다.
//
// WASAPI/XAudio2 는 COM 보일러플레이트가 크니, 클래식 PCM 재생 API `waveOut`(winmm)로 간단히.
// 짧은 톤을 미리 합성(사각/사인 + 감쇠)해 이벤트마다 fire-and-forget 재생한다.
// 헤드리스에서 검증 가능한 것: 장치 오픈·버퍼 제출이 에러 없이 되는지(소리 자체는 사람이 확인).
// 장치를 열지 않은 상태에서 Play() 는 조용한 no-op 이다 — 헤드리스 실행이
// 오디오 때문에 다른 코드 경로를 타지 않게 하려는 것이다.

#pragma once

namespace mega {
namespace platform {

enum class Sfx { Hit, Pickup, Death, Win, Step, Count };

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

#include <cmath>
#include <cstdint>
#include <vector>

namespace mega {
namespace platform {

class AudioDevice {
public:
    AudioDevice() = default;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    bool Init() {
        WAVEFORMATEX f{};
        f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = kRate;
        f.wBitsPerSample = 16; f.nBlockAlign = 2; f.nAvgBytesPerSec = kRate * 2;
        if (waveOutOpen(&hwo_, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
        Synth(Sfx::Hit,    200.0f, 0.07f, /*square*/ true,  0.0f);
        Synth(Sfx::Pickup, 700.0f, 0.10f, false,            900.0f);   // 상승음
        Synth(Sfx::Death,  120.0f, 0.45f, true,            -60.0f);    // 하강·길게
        Synth(Sfx::Win,    523.0f, 0.30f, false,            400.0f);   // 밝게 상승
        Synth(Sfx::Step,   90.0f,  0.03f, true,             0.0f);     // 짧은 클릭
        open_ = true;
        return true;
    }
    bool Opened() const { return open_; }

    void Play(Sfx s) {
        if (!open_ || s == Sfx::Count) return;
        for (auto& h : hdr_) {
            if (h.lpData == nullptr || (h.dwFlags & WHDR_DONE)) {
                if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hwo_, &h, sizeof(h));
                std::vector<int16_t>& b = buf_[(int)s];
                ZeroMemory(&h, sizeof(h));
                h.lpData = reinterpret_cast<LPSTR>(b.data());
                h.dwBufferLength = (DWORD)(b.size() * sizeof(int16_t));
                if (waveOutPrepareHeader(hwo_, &h, sizeof(h)) == MMSYSERR_NOERROR)
                    waveOutWrite(hwo_, &h, sizeof(h));
                return;
            }
        }
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
    static const int kRate = 22050;
    static const int kPool = 16;

    // freq(Hz)에서 시작, dur(초), square/sine, slide(끝까지 주파수 변화 Hz). 선형 감쇠 엔벨로프.
    void Synth(Sfx s, float freq, float dur, bool square, float slide) {
        int n = (int)(kRate * dur);
        std::vector<int16_t>& b = buf_[(int)s];
        b.resize(n);
        float phase = 0.0f;
        for (int i = 0; i < n; ++i) {
            float frac = (float)i / (n > 0 ? n : 1);
            float fr = freq + slide * frac;
            phase += fr / kRate;
            float v = square ? ((std::fmod(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f)
                             : std::sin(6.2831853f * phase);
            float env = 1.0f - frac;             // 선형 감쇠
            b[i] = (int16_t)(v * env * 8000.0f);
        }
    }

    HWAVEOUT             hwo_ = nullptr;
    bool                 open_ = false;
    std::vector<int16_t> buf_[(int)Sfx::Count];
    WAVEHDR              hdr_[kPool] = {};
};

}  // namespace platform
}  // namespace mega

#else  // 비-Windows: no-op 스텁

namespace mega {
namespace platform {

class AudioDevice {
public:
    AudioDevice() = default;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    bool Init() { return false; }
    bool Opened() const { return false; }
    void Play(Sfx) {}
    void Shutdown() {}
};

}  // namespace platform
}  // namespace mega

#endif
