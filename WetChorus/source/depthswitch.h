//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cbitmap.h"

#include <string>

namespace Yonie {

//------------------------------------------------------------------------
// DepthSwitch - the DEEP button: one latching push button with an indicator
// lamp above it.
//
// Two states, so it is a button rather than a knob. A two-position control on
// a knob would have to sit on the panel's seventeen-dot arc with fifteen of
// those dots meaning nothing, and "the pointer lands on a painted dot" is the
// rule the whole detent count came from.
//
// It differs from WetCompressor's ModeSwitch in one way that matters: there
// the buttons are painted into the backplate and the view only shades the
// pressed one. Here the panel paints a collar and a cap, but the cap has to
// MOVE, so this view draws it itself from a two-frame filmstrip rendered by
// the toolkit and lit by the same lamp as the knobs.
//
// STATE IS SHOWN TWICE, on purpose. The cap really does drop below its collar
// when pressed, which from directly overhead reads as the collar's inner walls
// appearing around it - correct, and subtle. Above it sits a red lamp in
// WetDelay's colours, which is the part of that cue anyone will actually
// notice. One is mechanical and honest, the other is unmissable.
//------------------------------------------------------------------------
class DepthSwitch : public VSTGUI::CControl
{
public:
    DepthSwitch(const VSTGUI::CRect& size);

    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;

    // "x0,y0,x1,y1" in view-local pixels: the button itself, for hit testing.
    void setButtonRect(const std::string& spec);
    // "x0,y0,x1,y1" in view-local pixels: where the filmstrip is drawn. The
    // view is BIGGER than this, because the lamp sits above the button and has
    // to be inside the view to be drawn and not clipped.
    void setFrameRect(const std::string& spec);
    // "x0,y0,x1,y1" in view-local pixels: where the lamp filmstrip is drawn.
    void setLED(const std::string& spec);
    // The lamp's own two-frame filmstrip. The view already has a background
    // bitmap - the button - so this one is carried separately.
    void setLEDBitmap(VSTGUI::CBitmap* b) { ledStrip = b; }
    VSTGUI::CBitmap* getLEDBitmap() const { return ledStrip; }
    // "x0,y0,x1,y1": where the glow sprite is drawn. Concentric with the lamp
    // and several times its size.
    void setGlow(const std::string& spec);
    void setGlowBitmap(VSTGUI::CBitmap* b) { glowSprite = b; }
    VSTGUI::CBitmap* getGlowBitmap() const { return glowSprite; }
    const std::string& glowSpec() const { return glowPosSpec; }

    const std::string& buttonSpec() const { return buttonRectSpec; }
    const std::string& frameSpec() const { return frameRectSpec; }
    const std::string& ledSpec() const { return ledPosSpec; }

    CLASS_METHODS(DepthSwitch, VSTGUI::CControl)

private:
    bool isDeep() const { return getValueNormalized() >= 0.5f; }

    VSTGUI::CRect button{0, 0, 0, 0};
    VSTGUI::CRect frame{0, 0, 0, 0};
    VSTGUI::CRect led{0, 0, 0, 0};
    VSTGUI::CRect glow{0, 0, 0, 0};
    VSTGUI::SharedPointer<VSTGUI::CBitmap> ledStrip;
    // The spill on the panel, as a sprite. It was six stacked filled ellipses:
    // that banded, it ended on a hard edge, and being flat colour at a flat
    // alpha it painted over the panel's grain, so the lamp read as a sticker
    // stuck on rather than as light falling on a surface. Baked, the falloff
    // is per-pixel and its alpha stays low enough across most of its area for
    // the grain to show straight through.
    VSTGUI::SharedPointer<VSTGUI::CBitmap> glowSprite;
    std::string buttonRectSpec, frameRectSpec, ledPosSpec, glowPosSpec;
};

//------------------------------------------------------------------------
} // namespace Yonie
