#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UIWidgets.h"

class Root;

class UndertowEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget
{
public:
    explicit UndertowEditor (UndertowProcessor&);
    ~UndertowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int baseW = 1200, baseH = 720;

private:
    UndertowProcessor& proc;
    UI::LookAndFeel lnf;
    std::unique_ptr<Root> root;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowEditor)
};
