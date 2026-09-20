//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"

#include "ledmeterview.h"
#include "chorusknob.h"
#include "depthswitch.h"

namespace Yonie {

//------------------------------------------------------------------------
// LEDMeterViewCreator - the same view the rest of the line uses, reused
// unchanged so the metering behaves identically across the plugins.
//------------------------------------------------------------------------
class LEDMeterViewCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    LEDMeterViewCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "LEDMeterView"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CControl"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "LED Meter View"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
    bool getAttributeValue(VSTGUI::CView* view, const string& attributeName,
                           string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override;
};

//------------------------------------------------------------------------
// ChorusKnobCreator - the stepped filmstrip knob. Two attributes: how many
// positions the strip holds, and how many of them make one ordinary detent.
//------------------------------------------------------------------------
class ChorusKnobCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    ChorusKnobCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "ChorusKnob"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CAnimKnob"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Chorus Knob"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
    bool getAttributeValue(VSTGUI::CView* view, const string& attributeName,
                           string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override;
};

//------------------------------------------------------------------------
// DepthSwitchCreator - the DEEP button and its lamp.
//------------------------------------------------------------------------
class DepthSwitchCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    DepthSwitchCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "DepthSwitch"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CControl"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Depth Switch"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
    bool getAttributeValue(VSTGUI::CView* view, const string& attributeName,
                           string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override;
};

//------------------------------------------------------------------------
void registerCustomViews();

//------------------------------------------------------------------------
} // namespace Yonie
