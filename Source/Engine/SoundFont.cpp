// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "SoundFont.h"

#include <cmath>
#include <cstring>

namespace tc
{
namespace
{
enum Gen
{
    StartAddrsOffset = 0, EndAddrsOffset = 1, StartloopAddrsOffset = 2, EndloopAddrsOffset = 3,
    StartAddrsCoarseOffset = 4, ModLfoToPitch = 5, VibLfoToPitch = 6, ModEnvToPitch = 7,
    InitialFilterFc = 8, InitialFilterQ = 9, ModLfoToFilterFc = 10, ModEnvToFilterFc = 11,
    EndAddrsCoarseOffset = 12, ModLfoToVolume = 13, ChorusEffectsSend = 15, ReverbEffectsSend = 16,
    Pan = 17, DelayModLFO = 21, FreqModLFO = 22, DelayVibLFO = 23, FreqVibLFO = 24,
    DelayModEnv = 25, AttackModEnv = 26, HoldModEnv = 27, DecayModEnv = 28, SustainModEnv = 29, ReleaseModEnv = 30,
    KeynumToModEnvHold = 31, KeynumToModEnvDecay = 32,
    DelayVolEnv = 33, AttackVolEnv = 34, HoldVolEnv = 35, DecayVolEnv = 36, SustainVolEnv = 37, ReleaseVolEnv = 38,
    KeynumToVolEnvHold = 39, KeynumToVolEnvDecay = 40,
    InstrumentGen = 41, KeyRange = 43, VelRange = 44, StartloopAddrsCoarseOffset = 45, Keynum = 46, Velocity = 47,
    InitialAttenuation = 48, EndloopAddrsCoarseOffset = 50, CoarseTune = 51, FineTune = 52, SampleID = 53,
    SampleModes = 54, ScaleTuning = 56, ExclusiveClass = 57, OverridingRootKey = 58
};

int16_t defaultGen (int g)
{
    switch (g)
    {
        case InitialFilterFc: return 13500;
        case DelayModLFO: case DelayVibLFO: case DelayModEnv: case AttackModEnv: case HoldModEnv: case DecayModEnv:
        case ReleaseModEnv: case DelayVolEnv: case AttackVolEnv: case HoldVolEnv: case DecayVolEnv: case ReleaseVolEnv:
            return -12000;
        case ScaleTuning: return 100;
        case Keynum: case Velocity: case OverridingRootKey: return -1;
        default: return 0;
    }
}

bool presetAdditive (int g)
{
    switch (g)
    {
        case StartAddrsOffset: case EndAddrsOffset: case StartloopAddrsOffset: case EndloopAddrsOffset:
        case StartAddrsCoarseOffset: case EndAddrsCoarseOffset: case StartloopAddrsCoarseOffset: case EndloopAddrsCoarseOffset:
        case InstrumentGen: case KeyRange: case VelRange: case Keynum: case Velocity: case SampleID: case SampleModes:
        case ExclusiveClass: case OverridingRootKey:
            return false;
        default: return true;
    }
}

uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

struct Chunk { const uint8_t* data = nullptr; uint32_t size = 0; };

float tcToSec (int tc) { return tc <= -32768 ? 0.0f : std::exp2 ((float) tc / 1200.0f); }
float absCentsToHz (int c) { return 8.176f * std::exp2 ((float) c / 1200.0f); }
} // namespace

std::shared_ptr<SoundFont> SoundFont::load (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "cannot read SoundFont"; return nullptr; }
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    const size_t n = mb.getSize();
    if (n < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "sfbk", 4) != 0) { error = "not a SoundFont 2 file"; return nullptr; }

    Chunk smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
    std::string bankName;

    // walk LIST chunks
    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const uint32_t sz = rd32 (d + pos + 4);
        if (pos + 8 + sz > n) break;
        if (std::memcmp (d + pos, "LIST", 4) == 0 && sz >= 4)
        {
            const uint8_t* list = d + pos + 8;
            size_t lp = 4;
            while (lp + 8 <= sz)
            {
                const uint32_t csz = rd32 (list + lp + 4);
                if (lp + 8 + csz > sz) break;
                const uint8_t* cd = list + lp + 8;
                const char* id = (const char*) (list + lp);
                if (std::memcmp (id, "smpl", 4) == 0) smpl = { cd, csz };
                else if (std::memcmp (id, "phdr", 4) == 0) phdr = { cd, csz };
                else if (std::memcmp (id, "pbag", 4) == 0) pbag = { cd, csz };
                else if (std::memcmp (id, "pgen", 4) == 0) pgen = { cd, csz };
                else if (std::memcmp (id, "inst", 4) == 0) inst = { cd, csz };
                else if (std::memcmp (id, "ibag", 4) == 0) ibag = { cd, csz };
                else if (std::memcmp (id, "igen", 4) == 0) igen = { cd, csz };
                else if (std::memcmp (id, "shdr", 4) == 0) shdr = { cd, csz };
                else if (std::memcmp (id, "INAM", 4) == 0) bankName.assign ((const char*) cd, strnlen ((const char*) cd, csz));
                lp += 8 + csz + (csz & 1);
            }
        }
        pos += 8 + sz + (sz & 1);
    }

    if (! smpl.data || ! phdr.data || ! pbag.data || ! pgen.data || ! inst.data || ! ibag.data || ! igen.data || ! shdr.data)
    {
        error = "incomplete SoundFont (SF3/compressed banks are not supported)";
        return nullptr;
    }

    auto sf = std::make_shared<SoundFont>();
    sf->name = bankName.empty() ? file.getFileNameWithoutExtension().toStdString() : bankName;
    sf->file = file;
    sf->sampleData.resize (smpl.size / 2 + 64, 0);
    std::memcpy (sf->sampleData.data(), smpl.data, (smpl.size / 2) * 2);

    // samples
    const size_t nsh = shdr.size / 46;
    for (size_t i = 0; i + 1 < nsh; ++i)
    {
        const uint8_t* p = shdr.data + i * 46;
        Sample s;
        s.start = rd32 (p + 20); s.end = rd32 (p + 24); s.loopStart = rd32 (p + 28); s.loopEnd = rd32 (p + 32);
        s.rate = rd32 (p + 36); s.root = p[40]; s.correction = (int8_t) p[41]; s.type = rd16 (p + 44);
        if (s.type & 0x10) { error = "compressed (SF3) samples are not supported"; return nullptr; }
        const uint32_t maxIdx = (uint32_t) (smpl.size / 2);
        s.end = std::min (s.end, maxIdx);
        s.start = std::min (s.start, s.end);
        if (s.rate == 0) s.rate = 44100;
        sf->samples.push_back (s);
    }

    auto readZones = [] (const Chunk& bag, const Chunk& gen, int bagStart, int bagEnd, int linkGen,
                         Zone& global, bool& hasGlobal, std::vector<Zone>& zones)
    {
        const int nbag = (int) (bag.size / 4);
        const int ngen = (int) (gen.size / 4);
        for (int b = bagStart; b < bagEnd && b + 1 < nbag; ++b)
        {
            const int g0 = rd16 (bag.data + b * 4), g1 = rd16 (bag.data + (b + 1) * 4);
            Zone z;
            bool hasLink = false;
            for (int g = g0; g < g1 && g < ngen; ++g)
            {
                const uint8_t* p = gen.data + g * 4;
                const int oper = rd16 (p);
                if (oper >= kNumGens) continue;
                if (oper == KeyRange) { z.keyLo = p[2]; z.keyHi = p[3]; }
                else if (oper == VelRange) { z.velLo = p[2]; z.velHi = p[3]; }
                else z.gen[oper] = (int16_t) rd16 (p + 2);
                z.setMask |= (1ull << oper);
                if (oper == linkGen) { z.link = rd16 (p + 2); hasLink = true; }
            }
            if (hasLink) zones.push_back (z);
            else if (b == bagStart) { global = z; hasGlobal = true; }
        }
    };

    const int ninst = (int) (inst.size / 22);
    for (int i = 0; i + 1 < ninst; ++i)
    {
        const uint8_t* p = inst.data + i * 22;
        Instrument in;
        in.name.assign ((const char*) p, strnlen ((const char*) p, 20));
        readZones (ibag, igen, rd16 (p + 20), rd16 (p + 22 + 20), SampleID, in.global, in.hasGlobal, in.zones);
        sf->instruments.push_back (std::move (in));
    }

    const int nph = (int) (phdr.size / 38);
    for (int i = 0; i + 1 < nph; ++i)
    {
        const uint8_t* p = phdr.data + i * 38;
        Preset pr;
        pr.name.assign ((const char*) p, strnlen ((const char*) p, 20));
        pr.program = rd16 (p + 20); pr.bank = rd16 (p + 22);
        readZones (pbag, pgen, rd16 (p + 24), rd16 (p + 38 + 24), InstrumentGen, pr.global, pr.hasGlobal, pr.zones);
        sf->presets.push_back (std::move (pr));
    }

    if (sf->presets.empty()) { error = "SoundFont has no presets"; return nullptr; }
    return sf;
}

const SoundFont::Preset* SoundFont::findPreset (int bank, int program) const
{
    for (auto& p : presets) if (p.bank == bank && p.program == program) return &p;
    return nullptr;
}

bool SoundFont::hasPreset (int bank, int program) const { return findPreset (bank, program) != nullptr; }

int SoundFont::resolve (int bank, int program, int key, int vel, VoiceParams* out, int maxOut) const
{
    const Preset* pr = findPreset (bank, program);
    if (! pr && bank == 128) pr = findPreset (128, 0);
    if (! pr && bank != 128) pr = findPreset (0, program);
    if (! pr && bank != 128)
        for (auto& p : presets) if (p.program == program && p.bank != 128) { pr = &p; break; }
    if (! pr) pr = findPreset (bank == 128 ? 128 : 0, 0);
    if (! pr) return 0;

    int count = 0;
    for (auto& pz : pr->zones)
    {
        if (key < pz.keyLo || key > pz.keyHi || vel < pz.velLo || vel > pz.velHi) continue;
        if (pr->hasGlobal && (key < pr->global.keyLo || key > pr->global.keyHi)) continue;
        if (pz.link < 0 || pz.link >= (int) instruments.size()) continue;
        const Instrument& in = instruments[(size_t) pz.link];

        for (auto& iz : in.zones)
        {
            if (key < iz.keyLo || key > iz.keyHi || vel < iz.velLo || vel > iz.velHi) continue;
            if (iz.link < 0 || iz.link >= (int) samples.size()) continue;
            if (count >= maxOut) return count;

            int g[kNumGens];
            for (int i = 0; i < kNumGens; ++i)
            {
                int v = defaultGen (i);
                if (in.hasGlobal && in.global.isSet (i)) v = in.global.gen[i];
                if (iz.isSet (i)) v = iz.gen[i];
                if (presetAdditive (i))
                {
                    int add = 0;
                    if (pr->hasGlobal && pr->global.isSet (i)) add = pr->global.gen[i];
                    if (pz.isSet (i)) add = pz.gen[i];
                    v += add;
                }
                g[i] = v;
            }

            const Sample& s = samples[(size_t) iz.link];
            VoiceParams& vp = out[count++];
            vp = VoiceParams();
            vp.data = sampleData.data();
            const int64_t start = (int64_t) s.start + g[StartAddrsOffset] + 32768LL * g[StartAddrsCoarseOffset];
            const int64_t end   = (int64_t) s.end + g[EndAddrsOffset] + 32768LL * g[EndAddrsCoarseOffset];
            const int64_t ls    = (int64_t) s.loopStart + g[StartloopAddrsOffset] + 32768LL * g[StartloopAddrsCoarseOffset];
            const int64_t le    = (int64_t) s.loopEnd + g[EndloopAddrsOffset] + 32768LL * g[EndloopAddrsCoarseOffset];
            const int64_t maxI  = (int64_t) sampleData.size() - 8;
            vp.start = (uint32_t) std::clamp<int64_t> (start, 0, maxI);
            vp.end = (uint32_t) std::clamp<int64_t> (end, vp.start + 1, maxI);
            vp.loopStart = (uint32_t) std::clamp<int64_t> (ls, vp.start, vp.end);
            vp.loopEnd = (uint32_t) std::clamp<int64_t> (le, vp.loopStart, vp.end);
            vp.loopMode = g[SampleModes] & 3;
            if (vp.loopEnd - vp.loopStart < 4) vp.loopMode = 0;
            vp.sampleRate = s.rate;
            vp.rootKey = (float) (g[OverridingRootKey] >= 0 ? g[OverridingRootKey] : s.root);
            vp.tuneCents = (float) (g[CoarseTune] * 100 + g[FineTune] + s.correction);
            vp.scaleTuning = (float) g[ScaleTuning];
            vp.attenuationDb = std::max (0.0f, (float) g[InitialAttenuation]) * 0.04f; // EMU-style 0.4 x centibels
            vp.pan = std::clamp ((float) g[Pan] / 1000.0f, -0.5f, 0.5f);
            vp.filterFc = std::min (20000.0f, absCentsToHz (g[InitialFilterFc]));
            vp.filterQ = std::max (0.707f, std::pow (10.0f, (float) g[InitialFilterQ] / 200.0f));

            const float keyFactor = (float) (60 - key);
            vp.delayVol   = tcToSec (g[DelayVolEnv]);
            vp.attackVol  = tcToSec (g[AttackVolEnv]);
            vp.holdVol    = tcToSec (g[HoldVolEnv] + (int) (g[KeynumToVolEnvHold] * keyFactor));
            vp.decayVol   = tcToSec (g[DecayVolEnv] + (int) (g[KeynumToVolEnvDecay] * keyFactor));
            vp.sustainVol = std::pow (10.0f, -std::clamp ((float) g[SustainVolEnv], 0.0f, 1440.0f) / 200.0f);
            vp.releaseVol = std::max (0.005f, tcToSec (g[ReleaseVolEnv]));

            vp.delayMod   = tcToSec (g[DelayModEnv]);
            vp.attackMod  = tcToSec (g[AttackModEnv]);
            vp.holdMod    = tcToSec (g[HoldModEnv] + (int) (g[KeynumToModEnvHold] * keyFactor));
            vp.decayMod   = tcToSec (g[DecayModEnv] + (int) (g[KeynumToModEnvDecay] * keyFactor));
            vp.sustainMod = 1.0f - std::clamp ((float) g[SustainModEnv], 0.0f, 1000.0f) / 1000.0f;
            vp.releaseMod = std::max (0.005f, tcToSec (g[ReleaseModEnv]));
            vp.modEnvToPitch = (float) g[ModEnvToPitch];
            vp.modEnvToFc = (float) g[ModEnvToFilterFc];

            vp.vibLfoDelay = tcToSec (g[DelayVibLFO]);
            vp.vibLfoFreq = absCentsToHz (g[FreqVibLFO]);
            vp.vibLfoToPitch = (float) g[VibLfoToPitch];
            vp.modLfoDelay = tcToSec (g[DelayModLFO]);
            vp.modLfoFreq = absCentsToHz (g[FreqModLFO]);
            vp.modLfoToPitch = (float) g[ModLfoToPitch];
            vp.modLfoToFc = (float) g[ModLfoToFilterFc];
            vp.modLfoToVolDb = (float) g[ModLfoToVolume] / 10.0f;

            vp.exclusiveClass = g[ExclusiveClass];
            vp.fixedKey = g[Keynum];
            vp.fixedVel = g[Velocity];
            vp.reverbSend = std::clamp ((float) g[ReverbEffectsSend] / 1000.0f, 0.0f, 1.0f);
            vp.chorusSend = std::clamp ((float) g[ChorusEffectsSend] / 1000.0f, 0.0f, 1.0f);
        }
    }
    return count;
}
} // namespace tc
