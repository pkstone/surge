/*
** Surge Synthesizer is Free and Open Source Software
**
** Surge is made available under the Gnu General Public License, v3.0
** https://www.gnu.org/licenses/gpl-3.0.en.html
**
** Copyright 2004-2020 by various individuals as described by the Git transaction log
**
** All source at: https://github.com/surge-synthesizer/surge.git
**
** Surge was a commercial product from 2004-2018, with Copyright and ownership
** in that period held by Claes Johanson at Vember Audio. Claes made Surge
** open source in September 2018.
*/

#include "FilterAnalysis.h"
#include "RuntimeFont.h"
#include "SkinColors.h"
#include <fmt/core.h>
#include "sst/filters/FilterPlotter.h"
#include <thread>

namespace Surge
{
namespace Overlays
{
struct FilterAnalysisEvaluator
{
    FilterAnalysis *an;
    FilterAnalysisEvaluator(FilterAnalysis *a) : an(a)
    {
        analysisThread = std::make_unique<std::thread>(callRunThread, this);
    }
    ~FilterAnalysisEvaluator()
    {
        {
            auto lock = std::unique_lock<std::mutex>(dataLock);
            continueWaiting = false;
        }
        cv.notify_one();
        analysisThread->join();
    }

    static void callRunThread(FilterAnalysisEvaluator *that) { that->runThread(); }
    void runThread()
    {
        uint64_t lastIB = 0;
        while (continueWaiting)
        {
            if (lastIB == inboundUpdates)
            {
                auto lock = std::unique_lock<std::mutex>(dataLock);
                cv.wait(lock);
            }

            if (lastIB != inboundUpdates)
            {
                int cty, csu;
                float ccu, cre;
                {
                    auto lock = std::unique_lock<std::mutex>(dataLock);
                    cty = type;
                    csu = subtype;
                    ccu = cutoff;
                    cre = resonance;
                    lastIB = inboundUpdates;
                }

                auto fp = sst::filters::FilterPlotter(15);
                // auto fp = sst::filters::DirectFilterPlotter();
                auto data = fp.plotFilterMagnitudeResponse(
                    (sst::filters::FilterType)cty, (sst::filters::FilterSubType)csu, ccu, cre);

                {
                    auto lock = std::unique_lock<std::mutex>(dataLock);
                    outboundUpdates++;
                    dataCopy = data;

                    juce::MessageManager::getInstance()->callAsync(
                        [safethat = juce::Component::SafePointer(an)] {
                            if (safethat)
                                safethat->repaint();
                        });
                }
            }
        }
    }

    void request(int t, int s, float c, float r)
    {
        {
            auto lock = std::unique_lock<std::mutex>(dataLock);

            type = t;
            subtype = s;
            cutoff = c;
            resonance = r;
            inboundUpdates++;
        }
        cv.notify_one();
    }

    std::pair<std::vector<float>, std::vector<float>> dataCopy;
    std::atomic<uint64_t> inboundUpdates{1}, outboundUpdates{1};
    int type{0}, subtype{0};
    float cutoff{60}, resonance{0};
    std::mutex dataLock;
    std::condition_variable cv;
    std::unique_ptr<std::thread> analysisThread;
    bool hasWork{false}, continueWaiting{true};
};

FilterAnalysis::FilterAnalysis(SurgeGUIEditor *e, SurgeStorage *s) : editor(e), storage(s)
{
    evaluator = std::make_unique<FilterAnalysisEvaluator>(this);
    f1Button = std::make_unique<Surge::Widgets::SelfDrawToggleButton>("Filter 1");
    f1Button->setStorage(storage);
    f1Button->setToggleState(true);
    f1Button->onToggle = [this]() { selectFilter(0); };
    addAndMakeVisible(*f1Button);

    f2Button = std::make_unique<Surge::Widgets::SelfDrawToggleButton>("Filter 2");
    f2Button->setStorage(storage);
    f2Button->setToggleState(true);
    f2Button->onToggle = [this]() { selectFilter(1); };
    addAndMakeVisible(*f2Button);

    selectFilter(0);
    repushData();
}

FilterAnalysis::~FilterAnalysis() = default;

void FilterAnalysis::onSkinChanged()
{
    f1Button->setSkin(skin, associatedBitmapStore);
    f2Button->setSkin(skin, associatedBitmapStore);
}

void FilterAnalysis::paint(juce::Graphics &g)
{
    auto &fs = editor->getPatch().scene[editor->current_scene].filterunit[whichFilter];

    static constexpr auto lowFreq = 10.f;
    static constexpr auto highFreq = 24000.f;
    static constexpr auto dbMin = -33.0f;
    static constexpr auto dbMax = 9.0f;
    constexpr auto dbRange = dbMax - dbMin;
    auto freqToX = [&](float freq, int width) {
        auto xNorm = std::log(freq / lowFreq) / std::log(highFreq / lowFreq);
        return xNorm * (float)width;
    };

    auto dbToY = [&](float db, int height) { return (float)height * (dbMax - db) / dbRange; };

    char nm[256], snm[256];
    fs.type.get_display(nm);
    fs.subtype.get_display(snm);

    auto label = fmt::format("{}", nm);
    if (sst::filters::fut_subcount[fs.type.val.i] > 0)
        label = fmt::format("{} ({})", nm, snm);

    g.fillAll(skin->getColor(Colors::MSEGEditor::Background));

    auto lb = getLocalBounds().transformedBy(getTransform().inverted());

    auto dRect = lb.withTrimmedTop(15).reduced(4);

    auto width = dRect.getWidth();
    auto height = dRect.getHeight();
    auto labelHeight = 9;
    auto font = Surge::GUI::getFontManager()->getLatoAtSize(7);

    {
        auto gs = juce::Graphics::ScopedSaveState(g);

        g.addTransform(juce::AffineTransform().translated(dRect.getX(), dRect.getY()));
        g.setFont(font);

        for (float freq : {100.0f, 1000.0f, 10000.0f})
        {
            const auto xPos = freqToX(freq, width);
            juce::Line line{juce::Point{xPos, 0.0f}, juce::Point{xPos, (float)height}};

            g.setColour(skin->getColor(Colors::MSEGEditor::Grid::SecondaryVertical));
            g.drawLine(line);

            const auto over1000 = freq >= 1000.0f;
            const auto freqString =
                juce::String(over1000 ? freq / 1000.0f : freq) + (over1000 ? " kHz" : " Hz");
            const auto labelRect = juce::Rectangle{font.getStringWidth(freqString), labelHeight}
                                       .withBottomY(height - 2)
                                       .withRightX((int)xPos);

            g.setColour(skin->getColor(Colors::MSEGEditor::Axis::Text));
            g.drawFittedText(freqString, labelRect, juce::Justification::bottom, 1);
        }

        for (float db : {-30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f})
        {
            const auto yPos = dbToY(db, height);
            juce::Line line{juce::Point{0.0f, yPos}, juce::Point{(float)width, yPos}};

            g.setColour(skin->getColor(Colors::MSEGEditor::Grid::SecondaryHorizontal));
            g.drawLine(line);

            const auto dbString = juce::String(db) + " dB";
            const auto labelRect = juce::Rectangle{font.getStringWidth(dbString), labelHeight}
                                       .withBottomY((int)yPos)
                                       .withRightX(width - 2);

            g.setColour(skin->getColor(Colors::MSEGEditor::Axis::Text));
            g.drawFittedText(dbString, labelRect, juce::Justification::right, 1);
        }
    }

    if (catchUpStore != evaluator->outboundUpdates)
    {
        std::pair<std::vector<float>, std::vector<float>> data;
        {
            auto lock = std::unique_lock(evaluator->dataLock);
            data = evaluator->dataCopy;
            catchUpStore = evaluator->outboundUpdates;
        }

        plotPath = juce::Path();

        auto [freqAxis, magResponseDBSmoothed] = data;
        bool started = false;
        const auto nPoints = freqAxis.size();
        for (int i = 0; i < nPoints; ++i)
        {
            if (freqAxis[i] < lowFreq / 2.0f || freqAxis[i] > highFreq * 1.01f)
                continue;

            auto xDraw = freqToX(freqAxis[i], dRect.getWidth());
            auto yDraw = dbToY(magResponseDBSmoothed[i], dRect.getHeight());

            xDraw += dRect.getX();
            yDraw += dRect.getY();
            if (!started)
            {
                plotPath.startNewSubPath(xDraw, yDraw);
                started = true;
            }
            else
            {
                plotPath.lineTo(xDraw, yDraw);
            }
        }
    }

    g.setColour(skin->getColor(Colors::MSEGEditor::Grid::Primary));
    g.drawRect(dRect);

    {
        auto gs = juce::Graphics::ScopedSaveState(g);
        auto fp = plotPath;
        fp.lineTo(dRect.getX() + width, dRect.getY() + height);
        fp.lineTo(0, dRect.getY() + height);

        g.reduceClipRegion(dRect);
        auto cg = juce::ColourGradient::vertical(
            skin->getColor(Colors::MSEGEditor::GradientFill::StartColor),
            skin->getColor(Colors::MSEGEditor::GradientFill::EndColor), dRect);

        g.setGradientFill(cg);
        g.fillPath(fp);
    }
    {
        auto gs = juce::Graphics::ScopedSaveState(g);
        g.reduceClipRegion(dRect);
        g.setColour(skin->getColor(Colors::MSEGEditor::Curve));

        g.strokePath(plotPath, juce::PathStrokeType(2.f, juce::PathStrokeType::JointStyle::curved));
    }
    auto txtr = lb.withHeight(15);
    // MSEG::Text is black - use the same color as the waveshaper preview for the title
    g.setColour(skin->getColor(Colors::Waveshaper::Preview::Text));
    g.setFont(skin->getFont(Fonts::Waveshaper::Preview::Title));
    g.drawText(label, txtr, juce::Justification::centred);
}

bool FilterAnalysis::shouldRepaintOnParamChange(const SurgePatch &patch, Parameter *p)
{
    if (p->ctrlgroup == cg_FILTER)
    {
        repushData();
        return true;
    }
    return false;
}

void FilterAnalysis::repushData()
{
    auto &fs = editor->getPatch().scene[editor->current_scene].filterunit[whichFilter];

    auto t = fs.type.val.i;
    auto s = fs.subtype.val.i;
    auto c = fs.cutoff.val.f;
    auto r = fs.resonance.val.f;

    evaluator->request(t, s, c, r);
}

void FilterAnalysis::selectFilter(int which)
{
    whichFilter = which;
    if (which == 0)
    {
        f1Button->setValue(1);
        f2Button->setValue(0);
    }
    else
    {
        f1Button->setValue(0);
        f2Button->setValue(1);
    }
    repushData();
    repaint();
}

void FilterAnalysis::resized()
{
    auto t = getTransform().inverted();
    auto h = getHeight();
    auto w = getWidth();
    t.transformPoint(w, h);

    f1Button->setBounds(2, 2, 40, 15);
    f2Button->setBounds(w - 42, 2, 40, 15);
}

} // namespace Overlays
}; // namespace Surge