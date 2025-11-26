#include "sf2exporter.h"
#include "apeloader.h"
#include "vagdecoder.h"
#include <sf2cute.hpp>
#include <cmath>
#include <QDebug>
#include <fstream>
#include <map>
#include <vector>

using namespace sf2cute;

// Convert Attack/Release rate (0x00-0x7F) to SF2 Timecents.
// SPU: 0x7F = Instant (Fastest), 0x00 = Infinite/Slowest.
// SF2: Timecents relative to 1 second. Lower/Negative = Faster.
static int16_t ps2RateToTimecents(uint8_t val) {
    if (val >= 0x7F) return -32768; // Instant attack/release
    if (val == 0) return 8000;      // Very slow (~100 seconds)

    // Approximation: Map 0-127 to seconds using a power curve
    // to mimic the SPU's exponential envelope hardware.
    // Invert: 127 is fast, 0 is slow.
    double normalized = (127.0 - val) / 127.0;
    double seconds = std::pow(normalized, 2.0) * 5.0; // Max approx 5s

    if (seconds < 0.001) return -32768;
    return static_cast<int16_t>(1200.0 * std::log2(seconds));
}

bool Sf2Exporter::exportToSf2(const QString& path, ApeLoader* loader) {
    // Initialize
    SoundFont sf2;
    sf2.set_sound_engine("Emu10k1");
    sf2.set_bank_name("Ape Escape 3 Export");
    sf2.set_rom_name("ROM");

    const auto& instruments = loader->getInstruments();

    // Cache to avoid duplicating sample data in file
    struct CachedSample {
        std::shared_ptr<SFSample> sample;
        bool loopEnabled;
    };
    std::map<uint16_t, CachedSample> sampleCache;

    int presetIdx = 0;

    for (size_t i = 0; i < instruments.size(); ++i) {
        QString instName = QString("Inst_%1").arg(i);
        std::shared_ptr<SFInstrument> sfInst = sf2.NewInstrument(instName.toStdString());

        for (const auto& part : instruments[i].parts) {
            std::shared_ptr<SFSample> sfSample;
            bool isLooping = false;

            // Create sample
            if (sampleCache.count(part.offset)) {
                sfSample = sampleCache[part.offset].sample;
                isLooping = sampleCache[part.offset].loopEnabled;
            } else {
                std::vector<uint8_t> vag = loader->extractVagSample(part.offset);
                if (vag.empty()) continue;

                // Decode
                VagResult res = VagDecoder::decode(vag);

                uint32_t ls = (res.loopStartSample > 0) ? res.loopStartSample : 0;
                uint32_t le = (res.loopEndSample > ls) ? res.loopEndSample : res.pcm.size();

                // Create SF2 Object
                sfSample = sf2.NewSample(
                    QString("Smp_%1").arg(part.offset).toStdString(),
                    res.pcm,
                    ls, le,
                    GEN_FREQ,
                    part.key_root,
                    part.cents
                    );

                // Store in cache
                sampleCache[part.offset] = { sfSample, res.loopEnabled };
                isLooping = res.loopEnabled;
            }

            // SF2 zone
            SFInstrumentZone zone(sfSample);

            // --- Generators ---

            // Loop
            if (isLooping) {
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kSampleModes, uint16_t(SampleMode::kLoopContinuously)));
            } else {
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kSampleModes, uint16_t(SampleMode::kNoLoop)));
            }

            // Key Range
            uint8_t kMin = part.key_min;
            uint8_t kMax = part.key_max;
            if (kMin > kMax) std::swap(kMin, kMax);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kKeyRange, RangesType(kMin, kMax)));

            // Pan: Convert 0..255 to -500..500
            // 128 is center.
            int pan = ((int)part.pan - 128) * 4;
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kPan, pan));

            // Reverb: 0x80+ means ON. Map to Effects Send 1 (Reverb).
            // No idea if 0x80+ intensifies the reverb
            if (part.reverb >= 0x80) {
                // 500 = 50.0% Send Amount
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kReverbEffectsSend, 500));
            }

            // ADSR
            // Attack
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kAttackVolEnv, ps2RateToTimecents(part.env_attack)));

            // Release
            uint8_t relRate = part.env_release_sustain & 0xFF;
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kReleaseVolEnv, ps2RateToTimecents(relRate)));

            // Volume Attenuation
            double volRatio = part.vol / 255.0;
            if (volRatio < 0.001) volRatio = 0.001;
            // Formula: -20 * log10(ratio) in centibels (x10)
            uint16_t atten = static_cast<uint16_t>(-200.0 * std::log10(volRatio));
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kInitialAttenuation, atten));

            sfInst->AddZone(std::move(zone));
        }

        // Preset
        std::shared_ptr<SFPreset> preset = sf2.NewPreset(QString("Preset %1").arg(i).toStdString(), presetIdx++, 0);
        SFPresetZone pZone(sfInst);
        pZone.SetGenerator(SFGeneratorItem(SFGenerator::kKeyRange, RangesType(0, 127)));
        preset->AddZone(std::move(pZone));
    }

    try {
        std::ofstream ofs(path.toStdString(), std::ios::binary);
        sf2.Write(ofs);
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Export Error:" << e.what();
        return false;
    }
}
