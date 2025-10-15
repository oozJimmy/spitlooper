#pragma once

#include <JuceHeader.h>

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent : public juce::AudioAppComponent
{
 public:
  //==============================================================================
  MainComponent();
  ~MainComponent() override;

  //==============================================================================
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
  void releaseResources() override;

  //==============================================================================
  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  void playButtonClicked();
  void loopButtonClicked();
  void copyLoopInputOverBuffer();

  std::atomic<bool> isPlaying;
  std::atomic<bool> isRecording;
  std::atomic<bool> inputLoopStored;

  juce::TextButton loopButton;
  juce::TextButton playButton;

  juce::AudioBuffer<float> loopInputBuffer;
  juce::AudioBuffer<float> loopBuffer;

  std::atomic<int> loopInputSampleIndex;
  std::atomic<int> loopSampleIndex;

  std::atomic<int> loopReadPosition;
  std::atomic<int> loopInputStartPosition;

  int crossfadeSamples;

  juce::Slider loopGain;
  juce::Slider inputGain;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
