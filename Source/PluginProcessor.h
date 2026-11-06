// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — audio plug-in processor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine/SongEngine.h"
#include "Engine/SoundFont.h"
#include "IO/GPReader.h"

class TablarionProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster
{
public:
    TablarionProcessor();
    ~TablarionProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Tablarion API (message thread)
    bool loadScoreFile (const juce::File& file, juce::String& error);
    bool loadScoreData (const juce::MemoryBlock& data, const juce::String& name, juce::String& error);
    /** Stops playback and unloads the current score (the plug-in returns to its empty state). */
    void closeScore();
    bool hasScore() const { return score != nullptr; }
    void loadSoundFontAsync (const juce::File& file);
    void clearSoundFont();
    void setSoundMode (tc::SoundMode m);
    tc::SoundMode getSoundMode() const { return soundMode; }
    void setTrackEngine (int track, tc::TrackEngine e);
    void setHumanize (float h);
    float getHumanize() const { return compileOptions.humanize; }
    void setAutoStrum (bool b);
    bool getAutoStrum() const { return compileOptions.autoStrum; }

    void play()   { transport.command = 1; }
    void stop()   { transport.command = 2; }
    void togglePlay() { transport.command = 3; }
    void seekTick (int64_t t) { transport.seekTick = t; }
    void seekBar (int playIndex);
    bool isPlaying() const { return transport.playing.load(); }
    int64_t positionTick() const { return transport.position.load(); }

    std::atomic<bool> followHost { false };

    /** Renders the whole song to a WAV file on a background thread. */
    void exportWavAsync (const juce::File& file, double sampleRate, std::function<void (bool, juce::String)> done);
    float getExportProgress() const { return exportState->load(); }

    std::shared_ptr<const tc::Score> getScore() const { return score; }
    std::shared_ptr<const tc::CompiledSong> getSong() const { return song; }
    std::shared_ptr<const tc::SoundFont> getSoundFont() const { return soundFont; }
    juce::String getScorePath() const { return scorePath; }
    juce::String getEngineName (int track) const;
    juce::String getStatus() const { return status; }
    juce::StringArray getRecentSoundFontCandidates() const;

    tc::EngineControls controls;
    tc::TransportRequests transport;
    juce::AudioProcessorValueTreeState params;

private:
    std::shared_ptr<const tc::Score> score;
    std::shared_ptr<const tc::CompiledSong> song;
    std::shared_ptr<const tc::SoundFont> soundFont;
    juce::MemoryBlock scoreData;
    juce::String scorePath, soundFontPath, status;
    tc::SoundMode soundMode = tc::SoundMode::Modeled;
    tc::CompileOptions compileOptions;

    juce::SpinLock engineLock;
    std::unique_ptr<tc::SongEngine> engine;
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);
    std::shared_ptr<std::atomic<float>> exportState = std::make_shared<std::atomic<float>> (-1.0f);
    double currentSampleRate = 0.0;
    int currentBlockSize = 512;
    juce::AudioBuffer<float> scratch;

    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* speedParam = nullptr;

    void recompile();
    void rebuildEngine();
    void autoLoadDefaultSoundFont();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablarionProcessor)
};
