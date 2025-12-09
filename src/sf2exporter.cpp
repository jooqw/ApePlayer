#include "sf2exporter.h"
#include "engine.h"
#include <sf2cute.hpp>
#include <QDebug>
#include <fstream>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace sf2cute;

struct CachedSample {
    std::shared_ptr<SFSample> sample;
    bool loopEnabled;
};

bool Sf2Exporter::exportToSf2(const QString& path, HDParser* hd, BDParser* bd) {
    SoundFont sf2;
    sf2.set_sound_engine("Emu10k1");
    sf2.set_bank_name("ApePlayer Export");
    sf2.set_rom_name("ROM");

    std::map<uint32_t, CachedSample> sampleCache;

    for (const auto& prog : hd->programs) {
        if (!prog) continue;

        QString instName = QString("Prg_%1").arg(prog->id);
        std::shared_ptr<SFInstrument> sfInst = sf2.NewInstrument(instName.toStdString());

        std::vector<bool> processed(prog->tones.size(), false);

        for (size_t i = 0; i < prog->tones.size(); ++i) {
            if (processed[i]) continue;
            processed[i] = true;

            const auto& t1 = prog->tones[i];

            int pairIdx = -1;
            bool isStereoPair = false;

            if (prog->is_layered && (i + 1 < prog->tones.size())) {
                const auto& t2 = prog->tones[i+1];

                // Criteria for merging:
                // 1. Same Key Range
                // 2. Same Root Key
                // 3. One is Left (<10), One is Right (>117)
                // 4. Same ADSR (approx)

                bool keysMatch = (t1.min_note == t2.min_note && t1.max_note == t2.max_note && t1.root_key == t2.root_key);
                bool panSplit = (std::abs((int)t1.pan - 0) < 20 && std::abs((int)t2.pan - 127) < 20) ||
                (std::abs((int)t1.pan - 127) < 20 && std::abs((int)t2.pan - 0) < 20);

                if (keysMatch && panSplit) {
                    pairIdx = i + 1;
                    isStereoPair = true;
                    processed[pairIdx] = true;
                }
            }

            auto addZone = [&](const Tone& t, int forcedPan = -1) {
                std::shared_ptr<SFSample> sfSample;
                bool isLooping = false;

                // 1. Get or Create Sample
                if (sampleCache.count(t.bd_offset)) {
                    sfSample = sampleCache[t.bd_offset].sample;
                    isLooping = sampleCache[t.bd_offset].loopEnabled;
                } else {
                    std::vector<uint8_t> raw = bd->get_adpcm_block(t.bd_offset);
                    if (raw.empty()) return; // Skip invalid data

                    DecodedSample res = EngineUtils::decode_adpcm(raw);
                    if (res.pcm.empty()) return;

                    uint32_t ls = (res.loop_start > 0) ? res.loop_start : 0;
                    uint32_t le = (res.loop_end > ls) ? res.loop_end : res.pcm.size();

                    sfSample = sf2.NewSample(
                        QString("Smp_%1").arg(t.bd_offset).toStdString(),
                                             res.pcm, ls, le, 44100,
                                             t.root_key > 0 ? t.root_key : 60,
                                             t.pitch_fine
                    );
                    sampleCache[t.bd_offset] = { sfSample, res.looping };
                    isLooping = res.looping;
                }

                // 2. Create Zone
                SFInstrumentZone zone(sfSample);

                // Loop Mode
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kSampleModes,
                                                  uint16_t(isLooping ? SampleMode::kLoopContinuously : SampleMode::kNoLoop)));

                // Key Range
                uint8_t kMin = t.min_note; uint8_t kMax = t.max_note;
                if (kMin > kMax) std::swap(kMin, kMax);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kKeyRange, RangesType(kMin, kMax)));

                // Pan (Use forcedPan if provided, else calculate)
                int panVal;
                if (forcedPan != -1) {
                    panVal = forcedPan;
                } else {
                    int p = ((int)t.pan + (int)prog->master_pan - 64);
                    panVal = (Util::clamp_pan(p) - 64) * 10;
                }
                panVal = std::clamp(panVal, -500, 500);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kPan, panVal));

                // Reverb
                if (t.is_reverb()) {
                    zone.SetGenerator(SFGeneratorItem(SFGenerator::kReverbEffectsSend, 500));
                }

                // ADSR (Hardware Simulation)
                u32 reg = ((u32)t.adsr2 << 16) | t.adsr1;

                int16_t att = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Attack);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kAttackVolEnv, att));

                int16_t dec = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Decay);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kDecayVolEnv, dec));

                int16_t rel = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Release);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kReleaseVolEnv, rel));

                // Sustain Level (Convert 0-15 to attenuation)
                u32 sl = t.adsr1 & 0x0F;
                uint16_t sf_sl = (15 - sl) * (1000 / 15);
                zone.SetGenerator(SFGeneratorItem(SFGenerator::kSustainVolEnv, sf_sl));

                sfInst->AddZone(std::move(zone));
            };


            if (isStereoPair) {
                const auto& t2 = prog->tones[pairIdx];

                if (t1.bd_offset == t2.bd_offset) {
                    //Fake Stereo
                    addZone(t1, 0);
                } else {
                    // True Stereo
                    addZone(t1);
                    addZone(t2);
                }
            } else {
                addZone(t1);
            }
        }

        // Create Preset for Instrument - use prog->id as the preset number
        std::shared_ptr<SFPreset> preset = sf2.NewPreset(QString("Preset %1").arg(prog->id).toStdString(), prog->id, 0);
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
