#include "sf2exporter.h"
#include "engine.h"
#include <sf2cute.hpp>
#include <QDebug>
#include <fstream>
#include <map>

using namespace sf2cute;

bool Sf2Exporter::exportToSf2(const QString& path, HDParser* hd, BDParser* bd) {
    SoundFont sf2;
    sf2.set_sound_engine("Emu10k1");
    sf2.set_bank_name("Ape Export");
    sf2.set_rom_name("ROM");

    struct CachedSample {
        std::shared_ptr<SFSample> sample;
        bool loopEnabled;
    };
    std::map<uint32_t, CachedSample> sampleCache;

    int presetIdx = 0;

    for (const auto& prog : hd->programs) {
        if (!prog) continue;

        QString instName = QString("Prg_%1").arg(prog->id);
        std::shared_ptr<SFInstrument> sfInst = sf2.NewInstrument(instName.toStdString());

        for (const auto& tone : prog->tones) {
            std::shared_ptr<SFSample> sfSample;
            bool isLooping = false;

            if (sampleCache.count(tone.bd_offset)) {
                sfSample = sampleCache[tone.bd_offset].sample;
                isLooping = sampleCache[tone.bd_offset].loopEnabled;
            } else {
                std::vector<uint8_t> raw = bd->get_adpcm_block(tone.bd_offset);
                if (raw.empty()) continue;

                DecodedSample res = EngineUtils::decode_adpcm(raw);
                if (res.pcm.empty()) continue;

                uint32_t ls = (res.loop_start > 0) ? res.loop_start : 0;
                uint32_t le = (res.loop_end > ls) ? res.loop_end : res.pcm.size();

                sfSample = sf2.NewSample(
                    QString("Smp_%1").arg(tone.bd_offset).toStdString(),
                                         res.pcm, ls, le, 44100,
                                         tone.root_key > 0 ? tone.root_key : 60,
                                         tone.pitch_fine
                );
                sampleCache[tone.bd_offset] = { sfSample, res.looping };
                isLooping = res.looping;
            }

            SFInstrumentZone zone(sfSample);

            zone.SetGenerator(SFGeneratorItem(SFGenerator::kSampleModes,
                                              uint16_t(isLooping ? SampleMode::kLoopContinuously : SampleMode::kNoLoop)));

            uint8_t kMin = tone.min_note; uint8_t kMax = tone.max_note;
            if (kMin > kMax) std::swap(kMin, kMax);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kKeyRange, RangesType(kMin, kMax)));

            int pan = ((int)tone.pan + (int)prog->master_pan - 64);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kPan, (Util::clamp_pan(pan) - 64) * 10));
            if (tone.is_reverb()) zone.SetGenerator(SFGeneratorItem(SFGenerator::kReverbEffectsSend, 500));

            u32 reg = ((u32)tone.adsr2 << 16) | tone.adsr1;

            int16_t attTime = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Attack);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kAttackVolEnv, attTime));

            int16_t decTime = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Decay);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kDecayVolEnv, decTime));

            int16_t relTime = EngineUtils::calculate_adsr_timecents(reg, HardwareADSR::Phase::Release);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kReleaseVolEnv, relTime));

            u32 sl = tone.adsr1 & 0x0F;
            uint16_t sf_sl = (15 - sl) * (1000 / 15);
            zone.SetGenerator(SFGeneratorItem(SFGenerator::kSustainVolEnv, sf_sl));

            sfInst->AddZone(std::move(zone));
        }

        std::shared_ptr<SFPreset> preset = sf2.NewPreset(QString("Preset %1").arg(prog->id).toStdString(), presetIdx++, 0);
        SFPresetZone pZone(sfInst);
        pZone.SetGenerator(SFGeneratorItem(SFGenerator::kKeyRange, RangesType(0, 127)));
        preset->AddZone(std::move(pZone));
    }

    try {
        std::ofstream ofs(path.toStdString(), std::ios::binary);
        sf2.Write(ofs);
        return true;
    } catch (...) { return false; }
}
