#include "core/nowplaying.h"
#include "core/log.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;

// Album art comes from whatever app (or web page) is playing media, so it is
// untrusted input. It is decoded by Windows' own image decoder (WIC, kept
// patched by Windows Update) rather than a bundled decoder, and capped: the
// encoded thumbnail must be small, its declared size sane, and it is decoded
// straight to at most kArtMaxSide pixels (the TV texture is only 512x288).
namespace {
constexpr uint64_t kArtMaxBytes  = 16ull * 1024 * 1024;   // encoded thumbnail
constexpr uint64_t kArtMaxPixels = 64ull * 1024 * 1024;   // declared w*h (64 MP)
constexpr uint32_t kArtMaxSide   = 1024;                  // decoded longest side

// Decode an album-art stream to RGBA8 (row 0 = top), at most kArtMaxSide px on
// the longest side. Returns false (and logs why) if it is rejected or invalid;
// WinRT errors are thrown to the caller.
bool decode_album_art(IRandomAccessStream const& stream, std::vector<unsigned char>& rgba,
                      int& outW, int& outH) {
    const uint64_t size = stream.Size();
    if (size == 0) return false;
    if (size > kArtMaxBytes) {
        LOGI("NowPlaying: album art ignored (%llu bytes, too large)", (unsigned long long)size);
        return false;
    }

    BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
    const uint32_t sw = decoder.PixelWidth(), sh = decoder.PixelHeight();
    if (sw == 0 || sh == 0 || (uint64_t)sw * sh > kArtMaxPixels) {
        LOGI("NowPlaying: album art ignored (%ux%u)", sw, sh);
        return false;
    }

    // Decode straight to a bounded size (aspect kept).
    uint32_t w = sw, h = sh;
    BitmapTransform transform;
    if (std::max(sw, sh) > kArtMaxSide) {
        const double k = (double)kArtMaxSide / std::max(sw, sh);
        w = std::max<uint32_t>(1, (uint32_t)std::lround(sw * k));
        h = std::max<uint32_t>(1, (uint32_t)std::lround(sh * k));
        transform.ScaledWidth(w);
        transform.ScaledHeight(h);
        transform.InterpolationMode(BitmapInterpolationMode::Fant);
    }
    // RGBA8, straight alpha, row 0 = top: the layout the renderer expects.
    PixelDataProvider pixels = decoder.GetPixelDataAsync(
        BitmapPixelFormat::Rgba8, BitmapAlphaMode::Straight, transform,
        ExifOrientationMode::IgnoreExifOrientation,
        ColorManagementMode::DoNotColorManage).get();
    com_array<uint8_t> px = pixels.DetachPixelData();
    if (px.size() != (size_t)w * h * 4) {
        LOGI("NowPlaying: album art decode returned an unexpected size");
        return false;
    }
    rgba.assign(px.begin(), px.end());
    outW = (int)w; outH = (int)h;
    LOGI("NowPlaying: decoded album art %ux%u -> %ux%u", sw, sh, w, h);
    return true;
}
}

bool NowPlaying::start() {
    if (running.load()) return true;
    running.store(true);
    worker = std::thread(&NowPlaying::threadMain, this);
    return true;
}

void NowPlaying::stop() {
    running.store(false);
    if (worker.joinable()) worker.join();
}

std::string NowPlaying::title()  { std::lock_guard<std::mutex> l(mtx); return curTitle;  }
std::string NowPlaying::artist() { std::lock_guard<std::mutex> l(mtx); return curArtist; }

bool NowPlaying::takeArtwork(std::vector<unsigned char>& rgba, int& w, int& h) {
    std::lock_guard<std::mutex> l(mtx);
    if (!pendingReady) return false;
    rgba = std::move(pendingRGBA);
    w = pendingW; h = pendingH;
    pendingReady = false;
    pendingRGBA.clear();
    return true;
}

void NowPlaying::threadMain() {
    init_apartment(apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    try {
        mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (hresult_error const& e) {
        LOGI("NowPlaying: session manager request failed (0x%08X)", (unsigned)e.code());
        uninit_apartment();
        return;
    } catch (...) {
        LOGI("NowPlaying: session manager request failed (unexpected error)");
        uninit_apartment();
        return;
    }
    LOGI("NowPlaying: SMTC session manager ready");

    // Decode the current track's thumbnail into the pending buffer. Returns true
    // only on success (art actually decoded & stored).
    auto tryFetchArt = [&](GlobalSystemMediaTransportControlsSessionMediaProperties const& props) -> bool {
        auto thumbRef = props.Thumbnail();
        if (!thumbRef) return false;
        try {
            IRandomAccessStreamWithContentType stream = thumbRef.OpenReadAsync().get();
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            if (!decode_album_art(stream, rgba, w, h)) return false;
            std::lock_guard<std::mutex> l(mtx);
            pendingRGBA = std::move(rgba);
            pendingW = w; pendingH = h; pendingReady = true;
            return true;
        } catch (hresult_error const& e) {
            LOGI("NowPlaying: thumbnail read/decode failed (0x%08X)", (unsigned)e.code());
            return false;
        } catch (...) {
            LOGI("NowPlaying: thumbnail read/decode failed (unexpected error)");
            return false;
        }
    };

    // Album art is often not ready on the first poll after a track change / app
    // launch, so we retry across polls until it lands (then poll lazily). Without
    // this the TV stays on the idle screen until you skip tracks.
    bool artDelivered = false;
    int  artTries = 0;
    const int kMaxArtTries = 25;   // ~7.5s of fast retries, then accept "no art"

    while (running.load()) {
        bool waitingForArt = false;
        try {
            GlobalSystemMediaTransportControlsSession session = mgr.GetCurrentSession();
            if (!session) {
                sessionActive.store(false);
                curTrackHasArt.store(false);
                lastTrackId.clear();            // reopening a player re-fetches art
                artDelivered = false; artTries = 0;
            } else {
                sessionActive.store(true);
                auto props = session.TryGetMediaPropertiesAsync().get();

                std::string title  = to_string(props.Title());
                std::string artist = to_string(props.Artist());
                std::string trackId = title + "\x1f" + artist;

                {
                    std::lock_guard<std::mutex> l(mtx);
                    curTitle = title; curArtist = artist;
                }

                if (trackId != lastTrackId) {
                    lastTrackId = trackId;
                    artDelivered = false; artTries = 0;
                    LOGI("NowPlaying: track -> '%s' / '%s'", title.c_str(), artist.c_str());
                }

                if (!artDelivered && artTries < kMaxArtTries) {
                    ++artTries;
                    if (tryFetchArt(props)) {
                        artDelivered = true;
                        curTrackHasArt.store(true);
                    } else {
                        waitingForArt = true;
                        if (artTries >= kMaxArtTries) curTrackHasArt.store(false);
                    }
                }
            }
        } catch (hresult_error const& e) {
            LOGI("NowPlaying: poll error (0x%08X)", (unsigned)e.code());
        } catch (...) {
            // Never let an error escape this thread: that would end the whole
            // process (and, under /lock, skip the normal exit path).
            LOGI("NowPlaying: poll error (unexpected)");
        }

        // Poll fast while chasing the artwork so it appears promptly on open;
        // otherwise idle at ~1s. Interruptible so stop() is responsive.
        const int pollMs = waitingForArt ? 300 : 1000;
        for (int i = 0; i < pollMs / 50 && running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    uninit_apartment();
}
