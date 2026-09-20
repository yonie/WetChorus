//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "customviewcreator.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cframe.h"

using namespace VSTGUI;

namespace Yonie {

static const std::string kAttrNumSegments = "num-segments";
static const std::string kAttrSegmentGap  = "segment-gap";
static const std::string kAttrHorizontal  = "horizontal";
static const std::string kAttrInverted    = "inverted";
static const std::string kAttrReduction   = "reduction-scheme";
static const std::string kAttrStepCount   = "step-count";
static const std::string kAttrCoarseStep  = "coarse-step";
static const std::string kAttrButtonRect  = "button-rect";
static const std::string kAttrLEDSpot     = "led-spot";
static const std::string kAttrFrameRect   = "frame-rect";
static const std::string kAttrLEDBitmap  = "led-bitmap";
static const std::string kAttrGlowSpot   = "glow-spot";
static const std::string kAttrGlowBitmap = "glow-bitmap";
static const std::string kAttrDbMin       = "db-min";
static const std::string kAttrDbMax       = "db-max";

//------------------------------------------------------------------------
// LEDMeterViewCreator
//------------------------------------------------------------------------
LEDMeterViewCreator::LEDMeterViewCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* LEDMeterViewCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new LEDMeterView(CRect(0, 0, 20, 200));
}

bool LEDMeterViewCreator::apply(CView* view, const UIAttributes& attributes,
                                const IUIDescription*) const
{
    auto* meter = dynamic_cast<LEDMeterView*>(view);
    if (!meter)
        return false;

    int32_t intValue;
    if (attributes.getIntegerAttribute(kAttrNumSegments, intValue))
        meter->setNumSegments(intValue);

    double doubleValue;
    if (attributes.getDoubleAttribute(kAttrSegmentGap, doubleValue))
        meter->setSegmentGap(static_cast<CCoord>(doubleValue));

    bool boolValue;
    if (attributes.getBooleanAttribute(kAttrHorizontal, boolValue))
        meter->setHorizontal(boolValue);
    if (attributes.getBooleanAttribute(kAttrInverted, boolValue))
        meter->setInverted(boolValue);
    if (attributes.getBooleanAttribute(kAttrReduction, boolValue))
        meter->setReductionScheme(boolValue);

    double lo = 0.0, hi = 0.0;
    if (attributes.getDoubleAttribute(kAttrDbMin, lo) &&
        attributes.getDoubleAttribute(kAttrDbMax, hi))
        meter->setDbRange(lo, hi);

    return true;
}

bool LEDMeterViewCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrNumSegments);
    names.emplace_back(kAttrSegmentGap);
    names.emplace_back(kAttrHorizontal);
    names.emplace_back(kAttrInverted);
    names.emplace_back(kAttrReduction);
    names.emplace_back(kAttrDbMin);
    names.emplace_back(kAttrDbMax);
    return true;
}

IViewCreator::AttrType LEDMeterViewCreator::getAttributeType(const string& name) const
{
    if (name == kAttrNumSegments) return kIntegerType;
    if (name == kAttrSegmentGap)  return kFloatType;
    if (name == kAttrHorizontal)  return kBooleanType;
    if (name == kAttrInverted)    return kBooleanType;
    if (name == kAttrReduction)   return kBooleanType;
    if (name == kAttrDbMin)       return kFloatType;
    if (name == kAttrDbMax)       return kFloatType;
    return kUnknownType;
}

bool LEDMeterViewCreator::getAttributeValue(CView* view, const string& name,
                                            string& value, const IUIDescription*) const
{
    auto* meter = dynamic_cast<LEDMeterView*>(view);
    if (!meter)
        return false;
    if (name == kAttrNumSegments) { value = "16";    return true; }
    if (name == kAttrSegmentGap)  { value = "3";     return true; }
    if (name == kAttrHorizontal)  { value = "false"; return true; }
    if (name == kAttrInverted)    { value = meter->isInverted() ? "true" : "false"; return true; }
    if (name == kAttrReduction)   { value = meter->isReductionScheme() ? "true" : "false"; return true; }
    if (name == kAttrDbMin)       { value = std::to_string(meter->dbMin()); return true; }
    if (name == kAttrDbMax)       { value = std::to_string(meter->dbMax()); return true; }
    return false;
}

//------------------------------------------------------------------------
// ChorusKnobCreator
//------------------------------------------------------------------------
ChorusKnobCreator::ChorusKnobCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* ChorusKnobCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new ChorusKnob(CRect(0, 0, 60, 60));
}

// UIViewFactory walks getBaseViewName() and runs the CAnimKnob and CControl
// creators for us, which is where the bitmap and the control tag come from.
// The only thing left is how many positions the knob has.
bool ChorusKnobCreator::apply(CView* view, const UIAttributes& attributes,
                              const IUIDescription*) const
{
    auto* knob = dynamic_cast<ChorusKnob*>(view);
    if (!knob)
        return false;

    int32_t steps = 0;
    if (attributes.getIntegerAttribute(kAttrStepCount, steps) && steps > 1)
        knob->setStepCount(steps);

    // How many of those steps make one ordinary detent. Absent means 1, so a
    // knob without the attribute has every step reachable and no modifier is
    // needed.
    int32_t coarse = 0;
    if (attributes.getIntegerAttribute(kAttrCoarseStep, coarse) && coarse > 1)
        knob->setCoarseStep(coarse);

    return true;
}

bool ChorusKnobCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrStepCount);
    names.emplace_back(kAttrCoarseStep);
    return true;
}

IViewCreator::AttrType ChorusKnobCreator::getAttributeType(const string& name) const
{
    if (name == kAttrStepCount)  return kIntegerType;
    if (name == kAttrCoarseStep) return kIntegerType;
    return kUnknownType;
}

bool ChorusKnobCreator::getAttributeValue(CView* view, const string& name,
                                          string& value, const IUIDescription*) const
{
    auto* knob = dynamic_cast<ChorusKnob*>(view);
    if (!knob)
        return false;
    if (name == kAttrStepCount)
    {
        value = std::to_string(knob->getStepCount());
        return true;
    }
    if (name == kAttrCoarseStep)
    {
        value = std::to_string(knob->getCoarseStep());
        return true;
    }
    return false;
}

//------------------------------------------------------------------------
// DepthSwitchCreator
//------------------------------------------------------------------------
DepthSwitchCreator::DepthSwitchCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* DepthSwitchCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new DepthSwitch(CRect(0, 0, 60, 90));
}

bool DepthSwitchCreator::apply(CView* view, const UIAttributes& attributes,
                               const IUIDescription* description) const
{
    auto* sw = dynamic_cast<DepthSwitch*>(view);
    if (!sw)
        return false;

    if (auto* s = attributes.getAttributeValue(kAttrButtonRect))
        sw->setButtonRect(*s);
    if (auto* s = attributes.getAttributeValue(kAttrFrameRect))
        sw->setFrameRect(*s);
    if (auto* s = attributes.getAttributeValue(kAttrLEDSpot))
        sw->setLED(*s);
    if (auto* s = attributes.getAttributeValue(kAttrLEDBitmap))
        if (description)
            sw->setLEDBitmap(description->getBitmap(s->c_str()));
    if (auto* s = attributes.getAttributeValue(kAttrGlowSpot))
        sw->setGlow(*s);
    if (auto* s = attributes.getAttributeValue(kAttrGlowBitmap))
        if (description)
            sw->setGlowBitmap(description->getBitmap(s->c_str()));
    return true;
}

bool DepthSwitchCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrButtonRect);
    names.emplace_back(kAttrLEDSpot);
    names.emplace_back(kAttrFrameRect);
    names.emplace_back(kAttrLEDBitmap);
    names.emplace_back(kAttrGlowSpot);
    names.emplace_back(kAttrGlowBitmap);
    return true;
}

IViewCreator::AttrType DepthSwitchCreator::getAttributeType(const string& name) const
{
    if (name == kAttrButtonRect) return kStringType;
    if (name == kAttrLEDSpot)    return kStringType;
    if (name == kAttrFrameRect)  return kStringType;
    if (name == kAttrLEDBitmap)  return kBitmapType;
    if (name == kAttrGlowSpot)   return kStringType;
    if (name == kAttrGlowBitmap) return kBitmapType;
    return kUnknownType;
}

bool DepthSwitchCreator::getAttributeValue(CView* view, const string& name,
                                           string& value, const IUIDescription*) const
{
    auto* sw = dynamic_cast<DepthSwitch*>(view);
    if (!sw)
        return false;
    if (name == kAttrButtonRect) { value = sw->buttonSpec(); return true; }
    if (name == kAttrLEDSpot)    { value = sw->ledSpec();    return true; }
    if (name == kAttrFrameRect)  { value = sw->frameSpec();  return true; }
    if (name == kAttrGlowSpot)   { value = sw->glowSpec();   return true; }
    return false;
}

//------------------------------------------------------------------------
void registerCustomViews()
{
    // Static so each creator registers exactly once, however many editor
    // windows the host opens.
    static LEDMeterViewCreator ledMeterCreator;
    static ChorusKnobCreator chorusKnobCreator;
    static DepthSwitchCreator depthSwitchCreator;

    // Belt as well as braces. ChorusKnob handles its own mouse and wheel, so
    // this no longer decides anything for the three knobs - but the frame-wide
    // default is still what any other knob-like control would inherit, and
    // circular is the wrong default for this panel.
    VSTGUI::CFrame::kDefaultKnobMode = VSTGUI::kLinearMode;
}

//------------------------------------------------------------------------
} // namespace Yonie
