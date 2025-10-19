#include "MainComponent.h"

#include "juce_graphics/juce_graphics.h"

//==============================================================================
MainComponent::MainComponent()
{
  isPlaying = false;
  isRecording = false;

  addAndMakeVisible(&playButton);
  playButton.setButtonText("Play");
  playButton.onClick = [this] { playButtonClicked(); };
  playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::deepskyblue);

  addAndMakeVisible(&loopButton);
  loopButton.setButtonText("loop");
  loopButton.onClick = [this] { loopButtonClicked(); };
  loopButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

  addAndMakeVisible(&loopGain);
  loopGain.setRange(0.0, 1.0);
  loopGain.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
  loopGain.setValue(0.5);
  loopGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

  addAndMakeVisible(&inputGain);
  inputGain.setRange(0.0, 1.0);
  inputGain.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
  inputGain.setValue(0.5);
  inputGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

  setSize(800, 250);

  if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio) &&
      !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
  {
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio,
                                      [&](bool granted) { setAudioChannels(granted ? 2 : 0, 2); });
  }
  else
  {
    // Specify the number of input and output channels that we want to open
    setAudioChannels(2, 2);
  }
}

MainComponent::~MainComponent()
{
  // This shuts down the audio device and clears the audio source.
  shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
  // This function will be called when the audio device is started, or when
  // its settings (i.e. sample rate, block size, etc) are changed.

  // You can use this function to initialise any resources you might need,
  // but be careful - it will be called on the audio thread, not the GUI thread.

  // For more details, see the help for AudioProcessor::prepareToPlay()
  loopInputSampleIndex.store(0);
  loopSampleIndex.store(0);
  loopReadPosition = 0;

  crossfadeSamples = (int)(0.0100f * (float)sampleRate);

  auto* device = deviceManager.getCurrentAudioDevice();

  auto input = device->getActiveInputChannels();
  auto output = device->getActiveOutputChannels();

  int inputChannels = input.getHighestBit();
  int outputChannels = output.getHighestBit();

  int initialDuration = 1;

  loopInputBuffer.setSize(inputChannels, sampleRate * initialDuration);
  loopInputBuffer.clear(0, loopInputBuffer.getNumSamples());

  loopBuffer.setSize(outputChannels, sampleRate * initialDuration);
  loopBuffer.clear(0, loopBuffer.getNumSamples());

  // REMOVE LATER - Fill loop buffer with Sin wave for testing the loop buffer playback
  // functionality
  float angleDelta = (1440.0f / (float)sampleRate) * juce::MathConstants<float>::twoPi;
  float currentAngle = 0.0f;

  for (int channel = 0; channel < outputChannels; channel++)
  {
    auto* loopWrite = loopBuffer.getWritePointer(channel, 0);
    for (int sample = 0; sample < loopBuffer.getNumSamples(); sample++)
    {
      loopWrite[sample] = std::sin(currentAngle) * 0.5f;
      currentAngle += angleDelta;
    }
  }
  loopSampleIndex.store(loopBuffer.getNumSamples());
  // End REMOVE
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
  auto maxInputChannels =
      deviceManager.getCurrentAudioDevice()->getActiveInputChannels().getHighestBit();

  bool recording = isRecording.load();
  bool playing = isPlaying.load();

  float inputGainVal = (float)inputGain.getValue();
  float loopGainVal = (float)loopGain.getValue();

  int loopSize = loopSampleIndex.load();
  int loopPosition = loopReadPosition.load();
  int loopSamplesRemaining = loopSize - loopPosition;
  auto outputSamplesRemaining = bufferToFill.buffer->getNumSamples();
  int sampleOffset = bufferToFill.startSample;

  while (outputSamplesRemaining > 0)
  {
    loopPosition = loopReadPosition.load();
    int samplesThisTime = outputSamplesRemaining;
    if (playing) samplesThisTime = juce::jmin(samplesThisTime, loopSamplesRemaining);

    for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); channel++)
    {
      // Read buffer pointers
      auto* inBuffer =
          bufferToFill.buffer->getReadPointer(channel % maxInputChannels, sampleOffset);
      auto* loopRead = loopBuffer.getReadPointer(channel % maxInputChannels, loopPosition);
      auto* loopCrossfadeRead = loopBuffer.getReadPointer(channel % maxInputChannels, 0);

      // Write buffer pointer
      auto* outBuffer = bufferToFill.buffer->getWritePointer(channel, sampleOffset);

      for (int sample = 0; sample < samplesThisTime; sample++)
      {
        if (playing)
        {
          if (loopSamplesRemaining - sample < crossfadeSamples)
          {
            // End of loop, crossfade in loop beginning
            auto fadeInGain = (float)(crossfadeSamples - (loopSamplesRemaining - sample)) /
                              (float)crossfadeSamples;
            auto fadeOutGain = (float)(loopSamplesRemaining - sample) / (float)crossfadeSamples;

            outBuffer[sample] =
                inBuffer[sample] * inputGainVal +
                (fadeOutGain * loopRead[sample] +
                 fadeInGain *
                     loopCrossfadeRead[sample + (crossfadeSamples - loopSamplesRemaining)]) *
                    loopGainVal;
          }
          else
            // Mix input with the loop buffer
            outBuffer[sample] = inBuffer[sample] * inputGainVal + loopRead[sample] * loopGainVal;
        }
        else
          // Forward input to output
          outBuffer[sample] = inBuffer[sample] * inputGainVal;
      }
    }

    outputSamplesRemaining -= samplesThisTime;
    sampleOffset += samplesThisTime;

    if (playing)
    {
      // Check for end of loop buffer, then update read position and remaining samples
      if (loopSamplesRemaining == samplesThisTime)
      {
        loopReadPosition.store(crossfadeSamples);
        loopSamplesRemaining = loopSampleIndex.load() + crossfadeSamples;
        // loopReadPosition.store(0);
        // loopSamplesRemaining = loopSampleIndex.load();
      }
      else
      {
        loopReadPosition.store(loopPosition + samplesThisTime);
        loopSamplesRemaining -= samplesThisTime;
      }
    }
  }

  // Save input to loopInputBuffer when recording is enabled
  if (recording)
  {
    recordBufferToInputLoop(bufferToFill.buffer, maxInputChannels);
  }

  //If loop is done recording move it to the main loop buffer
  //Only supports single layer looping
  if (inputLoopStored)
  {
    copyLoopInputOverBuffer();
    inputLoopStored.store(false);
  }
}

void MainComponent::releaseResources()
{
  // This will be called when the audio device stops, or when it is being
  // restarted due to a setting change.

  // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void MainComponent::resized()
{
  int height = 40;
  int width = 100;
  int margin = 10;
  int sliderHeight = 100;

  loopButton.setBounds(margin, margin, width, height);
  playButton.setBounds(margin, 2 * margin + height, width, height);

  loopGain.setBounds(2 * margin + width, margin, width, sliderHeight);
  inputGain.setBounds(2 * margin + width, 2 * margin + sliderHeight, width, sliderHeight);
}

void MainComponent::playButtonClicked()
{
  if (isPlaying.load())
  {
    isPlaying.store(false);
    playButton.setButtonText("Play");
  }
  else
  {
    isPlaying.store(true);
    playButton.setButtonText("Pause");
  }
}

void MainComponent::loopButtonClicked()
{
  if (isRecording.load())
  {
    // Set flag that user finished recording to loopInputBuffer
    inputLoopStored.store(true);

    isRecording.store(false);
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::black);
  }
  else
  {
    isRecording.store(true);
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
  }
}

void MainComponent::copyLoopInputOverBuffer()
{
  int numInputChannels = loopInputBuffer.getNumChannels();
  int numInputSamples = loopInputSampleIndex.load();

  loopBuffer.clear();
  loopSampleIndex.store(0);

  // Extend the loop buffer if the input is longer
  if (numInputSamples > loopBuffer.getNumSamples())
  {
    loopBuffer.setSize(loopBuffer.getNumChannels(), numInputSamples);
  }

  // Copies loopInputBuffer data into loopBuffer
  for (int channel = 0; channel < loopBuffer.getNumChannels(); channel++)
  {
    loopBuffer.copyFrom(channel, 0, loopInputBuffer, channel % numInputChannels, 0,
                        numInputSamples);
  }
  loopSampleIndex.store(loopInputSampleIndex);

  loopInputSampleIndex.store(0);
  loopInputBuffer.clear();
}

void MainComponent::recordBufferToInputLoop(juce::AudioBuffer<float>* buffer, int numInputChannels)
{
  int samplesThisTime = buffer->getNumSamples();
  int inputIndex = loopInputSampleIndex.load();

  // Expand loopInputBuffer by 2x if we need more sample space
  if (inputIndex + samplesThisTime >= loopInputBuffer.getNumSamples())
  {
    loopInputBuffer.setSize(loopInputBuffer.getNumChannels(), 2 * loopInputBuffer.getNumSamples(),
                            true, false, false);
  }

  // Copy input buffer to end of loopInputBuffer
  for (int channel = 0; channel < loopInputBuffer.getNumChannels(); channel++)
  {
    loopInputBuffer.copyFrom(channel, inputIndex, *buffer, channel % numInputChannels,
                             0, samplesThisTime);
  }

  loopInputSampleIndex.store(inputIndex + samplesThisTime);
}
