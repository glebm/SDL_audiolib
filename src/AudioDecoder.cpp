/*
  SDL_audiolib - An audio decoding, resampling and mixing library
  Copyright (C) 2014  Nikos Chantziaras

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
#include "Aulib/AudioDecoder.h"

#include <SDL_audio.h>
#include <SDL_rwops.h>

#include "aulib_global.h"
#include "aulib.h"
#include "aulib_config.h"
#include "Aulib/AudioDecoderVorbis.h"
#include "Aulib/AudioDecoderMpg123.h"
#include "Aulib/AudioDecoderModplug.h"
#include "Aulib/AudioDecoderBassmidi.h"
#include "Aulib/AudioDecoderWildmidi.h"
#include "Aulib/AudioDecoderFluidsynth.h"
#include "Aulib/AudioDecoderSndfile.h"
#include "Aulib/AudioDecoderOpus.h"
#include "Aulib/AudioDecoderMusepack.h"
#include "Aulib/AudioDecoderOpenmpt.h"
#include "Aulib/AudioDecoderXmp.h"
#include "Buffer.h"

namespace Aulib {

/// \private
struct AudioDecoder_priv final {
    Buffer<float> stereoBuf{0};
    bool isOpen = false;
};

} // namespace Aulib


Aulib::AudioDecoder::AudioDecoder()
    : d(std::make_unique<Aulib::AudioDecoder_priv>())
{ }


Aulib::AudioDecoder::~AudioDecoder()
{ }


std::unique_ptr<Aulib::AudioDecoder>
Aulib::AudioDecoder::decoderFor(const char* filename)
{
    const auto rwopsClose = [](SDL_RWops* rwops) { SDL_RWclose(rwops); };
    std::unique_ptr<SDL_RWops, decltype(rwopsClose)> rwops(SDL_RWFromFile(filename, "rb"), rwopsClose);
    return AudioDecoder::decoderFor(rwops.get());
}


std::unique_ptr<Aulib::AudioDecoder>
Aulib::AudioDecoder::decoderFor(SDL_RWops* rwops)
{
    std::unique_ptr<AudioDecoder> decoder;
    auto rwPos = SDL_RWtell(rwops);

#ifdef USE_DEC_LIBVORBIS
    decoder = std::make_unique<AudioDecoderVorbis>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderVorbis>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_LIBOPUSFILE
    decoder = std::make_unique<AudioDecoderOpus>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderOpus>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_MUSEPACK
    decoder = std::make_unique<AudioDecoderMusepack>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderMusepack>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#if defined(USE_DEC_FLUIDSYNTH) or defined(USE_DEC_BASSMIDI) or defined(USE_DEC_WILDMIDI)
    {
        char head[4];
        if (SDL_RWread(rwops, head, 1, 4) == 4 && strncmp(head, "MThd", 4) == 0) {
            SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#   ifdef USE_DEC_FLUIDSYNTH
            decoder = std::make_unique<AudioDecoderFluidSynth>();
            if (decoder->open(rwops)) {
                return std::make_unique<Aulib::AudioDecoderFluidSynth>();
            }
#   elif defined(USE_DEC_BASSMIDI)
            decoder = std::make_unique<AudioDecoderBassmidi>();
            if (decoder->open(rwops)) {
                return std::make_unique<Aulib::AudioDecoderBassmidi>();
            }
#   elif defined(USE_DEC_WILDMIDI)
            decoder = std::make_unique<AudioDecoderWildmidi>();
            if (decoder->open(rwops)) {
                return std::make_unique<Aulib::AudioDecoderWildmidi>();
            }
#   endif
        }
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_SNDFILE
    decoder = std::make_unique<AudioDecoderSndfile>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderSndfile>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_OPENMPT
    decoder = std::make_unique<AudioDecoderOpenmpt>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderOpenmpt>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_XMP
    decoder = std::make_unique<AudioDecoderXmp>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderXmp>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

#ifdef USE_DEC_MODPLUG
    // We don't try ModPlug, since it thinks just about anything is a module
    // file, which would result in virtually everything we feed it giving a
    // false positive.
#endif

#ifdef USE_DEC_MPG123
    decoder = std::make_unique<AudioDecoderMpg123>();
    if (decoder->open(rwops)) {
        return std::make_unique<Aulib::AudioDecoderMpg123>();
    }
    SDL_RWseek(rwops, rwPos, RW_SEEK_SET);
#endif

    return nullptr;
}


bool
Aulib::AudioDecoder::isOpen() const
{
    return d->isOpen;
}


// Conversion happens in-place.
static void
monoToStereo(float buf[], int len)
{
    if (len < 1 or buf == nullptr) {
        return;
    }
    for (int i = len / 2 - 1, j = len - 1; i > 0; --i) {
        buf[j--] = buf[i];
        buf[j--] = buf[i];
    }
}


static void
stereoToMono(float dst[], float src[], int srcLen)
{
    if (srcLen < 1 or dst == nullptr or src == nullptr) {
        return;
    }
    for (int i = 0, j = 0; i < srcLen; i += 2, ++j) {
        dst[j] = src[i] * 0.5f;
        dst[j] += src[i + 1] * 0.5f;
    }
}


int
Aulib::AudioDecoder::decode(float buf[], int len, bool& callAgain)
{
    const SDL_AudioSpec& spec = Aulib::spec();

    if (this->getChannels() == 1 and spec.channels == 2) {
        int srcLen = this->doDecoding(buf, len / 2, callAgain);
        monoToStereo(buf, srcLen * 2);
        return srcLen * 2;
    }

    if (this->getChannels() == 2 and spec.channels == 1) {
        if (d->stereoBuf.size() != len * 2) {
            d->stereoBuf.reset(len * 2);
        }
        int srcLen = this->doDecoding(d->stereoBuf.get(), d->stereoBuf.size(), callAgain);
        stereoToMono(buf, d->stereoBuf.get(), srcLen);
        return srcLen / 2;
    }
    return this->doDecoding(buf, len, callAgain);
}


void
Aulib::AudioDecoder::setIsOpen(bool f)
{
    d->isOpen = f;
}
