//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"

namespace Yonie {

//------------------------------------------------------------------------
// LEDMeterView - Custom 80's style LED segment meter
// Displays 12 discrete LED segments: 8 green, 2 yellow, 2 red
//------------------------------------------------------------------------
class LEDMeterView : public VSTGUI::CControl
{
public:
    LEDMeterView(const VSTGUI::CRect& size);
    ~LEDMeterView() override = default;

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    
    // Configuration
    void setNumSegments(int num) { numSegments = num; }
    void setSegmentGap(VSTGUI::CCoord gap) { segmentGap = gap; }
    void setHorizontal(bool horiz) { isHorizontal = horiz; }

    // A gain-reduction strip is not a level strip upside down in colour terms
    // only - it also fills from the OTHER end. The panel prints its scale 0 at
    // the top running down to -22, so segment 0 is the top one and the stack
    // grows downwards as the compressor works.
    void setInverted(bool inv) { inverted = inv; }
    bool isInverted() const { return inverted; }

    // "level" = green/yellow/red, the house metering. "reduction" = one colour
    // all the way down, because gain reduction has no danger zone: 12 dB is not
    // worse than 3, it is just more.
    void setReductionScheme(bool on) { reduction = on; }
    bool isReductionScheme() const { return reduction; }

    // Map the incoming value as dB against the scale PRINTED BESIDE THE METER,
    // rather than linearly across the segments.
    //
    // Linear is what this did, and it disagrees with its own panel: the art
    // prints +18 at the top and -48 at the bottom, but a linear map puts half
    // scale - which is -6 dBFS - at the middle segment, so a healthy signal lit
    // three LEDs and read as nearly silent. A meter that does not agree with
    // the numbers next to it is worse than no meter.
    //
    // Off by default, so a meter with no printed scale keeps the old
    // behaviour and nothing already shipped moves.
    void setDbRange(double lo, double hi) { dbLo = lo; dbHi = hi; dbScale = hi > lo; }
    bool hasDbRange() const { return dbScale; }
    double dbMin() const { return dbLo; }
    double dbMax() const { return dbHi; }

    // Number of segments lit for the current value.
    int litSegments() const;
    
    // Custom view class name for VSTGUI factory
    CLASS_METHODS(LEDMeterView, CControl)

protected:
    VSTGUI::CRect calculateSegmentRect(int segmentIndex) const;
    VSTGUI::CColor getLitColor(int segmentIndex) const;
    VSTGUI::CColor getDarkColor(int segmentIndex) const;
    
    int numSegments = 12;
    VSTGUI::CCoord segmentGap = 2;
    bool isHorizontal = true;
    bool inverted = false;
    bool reduction = false;
    bool dbScale = false;
    double dbLo = -48.0, dbHi = 18.0;

    // The GR strip's amber. Warmer than the level meters' yellow, so the two
    // do not read as the same lamp doing two jobs.
    VSTGUI::CColor amberLit{255, 176, 26, 255};
    VSTGUI::CColor amberDark{44, 31, 8, 255};
    
    // Color definitions for 80's LED look
    // Green zone (segments 0-7)
    // The panel art paints every LED lit, so the meter must black its own
    // window out before drawing. Without this the painted LEDs show through
    // the gaps between segments as bright slivers and the meter reads as
    // permanently pinned.
    VSTGUI::CColor windowBlack{0, 0, 0, 255};

    // A lit LED spills light onto the black around it. Drawn as a few
    // translucent rings stepping outwards from each lit segment, brightest
    // against the segment. Only lit ones glow - an unlit LED emits nothing.
    int glowSpread = 3;         // px the spill reaches
    uint8_t glowAlpha = 42;     // alpha of the innermost ring

    VSTGUI::CColor greenLit{0, 255, 0, 255};
    VSTGUI::CColor greenDark{10, 42, 10, 255};
    
    // Yellow zone (segments 8-9)
    VSTGUI::CColor yellowLit{255, 255, 0, 255};
    VSTGUI::CColor yellowDark{42, 42, 10, 255};
    
    // Red zone (segments 10-11)
    VSTGUI::CColor redLit{255, 0, 0, 255};
    VSTGUI::CColor redDark{42, 10, 10, 255};
};

//------------------------------------------------------------------------
} // namespace Yonie