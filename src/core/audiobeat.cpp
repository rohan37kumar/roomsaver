#include "core/audiobeat.h"
#include "core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

#include "kiss_fftr.h"

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <chrono>

template <class T> static void safe_release(T*& p) { if (p) { p->Release(); p = nullptr; } }

void AudioBeat::threadMain() {
    // An error escaping a std::thread ends the whole process (and, under /lock,
    // skips the normal exit path), so contain it: the beat light just goes off.
    try {
        capture();
    } catch (...) {
        LOGI("AudioBeat: unexpected error - beat detection stopped");
    }
    capturing.store(false);
    beat.store(0.0f);
}

void AudioBeat::capture() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInit = SUCCEEDED(hr);

    // ---- Persistent DSP state (survives device reconnects) ----
    const int N = 1024;                       // FFT window (~21 ms @ 48 kHz)
    kiss_fftr_cfg cfg = kiss_fftr_alloc(N, 0, nullptr, nullptr);
    if (!cfg) {
        LOGI("AudioBeat: FFT setup failed - beat detection off");
        if (comInit) CoUninitialize();
        return;
    }
    std::vector<float> hann(N);
    for (int i = 0; i < N; ++i) hann[i] = 0.5f * (1.0f - cosf(6.2831853f * i / (N - 1)));
    std::vector<float> tin(N);
    std::vector<kiss_fft_cpx> freq(N / 2 + 1);

    float env = 0.0f;         // onset envelope (kept across reconnects; decays)

    // Sleep in short slices so stop() joins promptly even mid-wait.
    auto interruptibleWait = [&](int ms) {
        for (int slept = 0; slept < ms && running.load(); slept += 40)
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
    };

    bool loggedWaiting = false;   // throttle the "waiting for device" log spam

    // Outer loop: (re)connect to the current default render endpoint and capture
    // until the device fails/changes, then retry. This makes the beat detector
    // resilient to (a) the Windows Audio service / default endpoint not being
    // ready yet when the saver starts, and (b) the output device being swapped
    // (Bluetooth/HDMI/USB) while the saver runs. Previously this thread
    // initialized ONCE and exited permanently on any failure -> at login the
    // audio stack often isn't up yet, so the beat pulsing was dead until the
    // process was relaunched (e.g. re-running /c). Do not revert to one-shot init.
    while (running.load()) {
        IMMDeviceEnumerator* pEnum    = nullptr;
        IMMDevice*           pDevice  = nullptr;
        IAudioClient*        pClient  = nullptr;
        IAudioCaptureClient* pCapture = nullptr;
        WAVEFORMATEX*        pwfx     = nullptr;

        auto releaseDevice = [&]() {
            if (pClient) pClient->Stop();
            if (pwfx) { CoTaskMemFree(pwfx); pwfx = nullptr; }
            safe_release(pCapture); safe_release(pClient);
            safe_release(pDevice);  safe_release(pEnum);
        };
        // On any setup failure: drop the light, release, wait, then retry.
        auto retryAfterFailure = [&](const char* what, HRESULT code) {
            if (!loggedWaiting)
                LOGI("AudioBeat: %s (0x%08X) — audio not ready, retrying until it is",
                     what, (unsigned)code);
            loggedWaiting = true;
            releaseDevice();
            capturing.store(false);
            beat.store(0.0f);
            interruptibleWait(1000);
        };

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
        if (FAILED(hr)) { retryAfterFailure("CoCreateInstance failed", hr); continue; }
        hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (FAILED(hr)) { retryAfterFailure("no default render endpoint", hr); continue; }
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient);
        if (FAILED(hr)) { retryAfterFailure("Activate failed", hr); continue; }
        hr = pClient->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) { retryAfterFailure("GetMixFormat failed", hr); continue; }

        const int channels   = pwfx->nChannels;
        const int sampleRate = (int)pwfx->nSamplesPerSec;
        const int bits       = pwfx->wBitsPerSample;
        const int stride     = pwfx->nBlockAlign;
        bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
            isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }

        const REFERENCE_TIME hnsBuffer = 2000000;  // 200 ms
        hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 hnsBuffer, 0, pwfx, nullptr);
        if (FAILED(hr)) { retryAfterFailure("Initialize failed", hr); continue; }
        hr = pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCapture);
        if (FAILED(hr)) { retryAfterFailure("GetService failed", hr); continue; }
        hr = pClient->Start();
        if (FAILED(hr)) { retryAfterFailure("Start failed", hr); continue; }

        // ---- Connected. ----
        loggedWaiting = false;
        capturing.store(true);
        LOGI("AudioBeat: loopback started (%d Hz, %d ch, %d-bit, float=%d)",
             sampleRate, channels, bits, (int)isFloat);

        const float binHz  = (float)sampleRate / N;
        const int   lowBin = std::max(1, (int)floorf(35.0f / binHz));
        const int   highBin = std::min(N / 2, (int)ceilf(160.0f / binHz));

        std::vector<float> accum;     // mono sample queue (fresh per connection)
        float avgEnergy = 0.0f;       // adaptive baseline (re-adapts to this device)

        bool deviceLost = false;
        while (running.load() && !deviceLost) {
            UINT32 packet = 0;
            if (FAILED(pCapture->GetNextPacketSize(&packet))) { deviceLost = true; break; }

            if (packet == 0) {
                // No audio flowing: let the light settle toward off.
                float e = beat.load() * 0.90f;
                beat.store(e < 0.001f ? 0.0f : e);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }

            while (packet > 0 && running.load()) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(pCapture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { deviceLost = true; break; }
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

                for (UINT32 f = 0; f < frames; ++f) {
                    float mono = 0.0f;
                    if (!silent && data) {
                        const BYTE* fr = data + (size_t)f * stride;
                        if (isFloat) {
                            const float* fp = (const float*)fr;
                            for (int c = 0; c < channels; ++c) mono += fp[c];
                        } else if (bits == 16) {
                            const int16_t* sp = (const int16_t*)fr;
                            for (int c = 0; c < channels; ++c) mono += sp[c] / 32768.0f;
                        } else if (bits == 32) {
                            const int32_t* ip = (const int32_t*)fr;
                            for (int c = 0; c < channels; ++c) mono += ip[c] / 2147483648.0f;
                        }
                        mono /= (float)channels;
                    }
                    accum.push_back(mono);
                }
                pCapture->ReleaseBuffer(frames);

                // Process complete, non-overlapping windows.
                while ((int)accum.size() >= N) {
                    for (int i = 0; i < N; ++i) tin[i] = accum[i] * hann[i];
                    kiss_fftr(cfg, tin.data(), freq.data());

                    float energy = 0.0f;
                    for (int k = lowBin; k <= highBin; ++k)
                        energy += sqrtf(freq[k].r * freq[k].r + freq[k].i * freq[k].i);

                    avgEnergy = avgEnergy * 0.97f + energy * 0.03f;
                    float norm = (avgEnergy > 1e-6f) ? energy / avgEnergy : 0.0f;
                    // Lower threshold + tighter range => catches more of the beat; the
                    // adaptive baseline keeps it from pinning on loud/quiet tracks.
                    float target = std::clamp((norm - 1.15f) / 1.25f, 0.0f, 1.0f) * sensitivity;
                    target = std::clamp(target, 0.0f, 1.0f);

                    if (target > env) env += (target - env) * 0.80f;  // snappy attack
                    else              env *= 0.86f;                    // smooth-ish decay
                    beat.store(std::clamp(env, 0.0f, 1.0f));

                    accum.erase(accum.begin(), accum.begin() + N);
                }

                if (FAILED(pCapture->GetNextPacketSize(&packet))) { deviceLost = true; break; }
            }
        }

        // Session ended: either we're stopping, or the device was lost/changed.
        releaseDevice();
        capturing.store(false);
        beat.store(0.0f);

        if (deviceLost && running.load()) {
            LOGI("AudioBeat: capture device lost/changed — reconnecting");
            interruptibleWait(400);
        }
    }

    if (cfg) kiss_fftr_free(cfg);
    if (comInit) CoUninitialize();
    capturing.store(false);
    beat.store(0.0f);
}

bool AudioBeat::start() {
    if (running.load()) return true;
    running.store(true);
    worker = std::thread(&AudioBeat::threadMain, this);
    return true;
}

void AudioBeat::stop() {
    running.store(false);
    if (worker.joinable()) worker.join();
}
