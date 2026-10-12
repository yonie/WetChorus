//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "ledmeterview.h"
#include "meterlevel.h"

#include <cmath>

using namespace VSTGUI;

namespace Yonie {

//------------------------------------------------------------------------
// LEDMeterView Implementation
//------------------------------------------------------------------------
LEDMeterView::LEDMeterView(const CRect& size)
    : CControl(size, nullptr, -1)
{
    setWantsFocus(false);
}

//------------------------------------------------------------------------
int LEDMeterView::litSegments() const
{
    const float level = getValueNormalized();

    // Gain reduction: a linear share of the printed range.
    if (reduction)
    {
        const int n = static_cast<int>(level * numSegments + 0.5f);
        return n < 0 ? 0 : n > numSegments ? numSegments : n;
    }

    // A level meter: the line's one scale, only the block count is ours (meterlevel.h).
    return Meter::litSegments(level, numSegments);
}

//------------------------------------------------------------------------
void LEDMeterView::draw(CDrawContext* context)
{
    const int litCount = litSegments();
    
    // Black the whole window out first. The backplate has all 12 LEDs painted
    // lit, and the drawn segments do not line up with the painted ones, so
    // anything left uncovered shows through as a bright sliver.
    context->setFillColor(windowBlack);
    context->drawRect(getViewSize(), kDrawFilled);

    // Glow first, so the segments themselves land on top of it and stay crisp.
    // Rings step outwards from each lit segment with the alpha falling off, and
    // they are drawn for every lit LED before any segment is filled - otherwise
    // a neighbour's glow would paint over the LED below it.
    for (int i = 0; i < litCount; i++)
    {
        CRect seg = calculateSegmentRect(i);
        CColor glow = getLitColor(i);
        for (int s = glowSpread; s >= 1; s--)
        {
            CRect halo = seg;
            halo.extend(static_cast<CCoord>(s), static_cast<CCoord>(s));
            glow.alpha = static_cast<uint8_t>(glowAlpha * (glowSpread - s + 1)
                                              / (glowSpread + 1));
            context->setFillColor(glow);
            context->drawRect(halo, kDrawFilled);
        }
    }

    // Draw each segment
    for (int i = 0; i < numSegments; i++)
    {
        CRect segRect = calculateSegmentRect(i);
        CColor color = (i < litCount) ? getLitColor(i) : getDarkColor(i);
        
        context->setFillColor(color);
        context->drawRect(segRect, kDrawFilled);
    }
    
    setDirty(false);
}

//------------------------------------------------------------------------
CRect LEDMeterView::calculateSegmentRect(int segmentIndex) const
{
    CRect viewSize = getViewSize();
    CCoord totalWidth = viewSize.getWidth();
    CCoord totalHeight = viewSize.getHeight();
    
    // inverted flips which end index 0 sits at: a level meter starts at the
    // bottom (or the left) and grows towards the danger end, a gain-reduction
    // meter starts at 0 dB and grows downwards.
    const int i = inverted ? (numSegments - 1 - segmentIndex) : segmentIndex;

    if (isHorizontal)
    {
        // Horizontal meter: segments arranged left to right
        CCoord segmentWidth = (totalWidth - (numSegments - 1) * segmentGap) / numSegments;
        CCoord x = viewSize.left + i * (segmentWidth + segmentGap);
        
        return CRect(x, viewSize.top, x + segmentWidth, viewSize.bottom);
    }
    else
    {
        // Vertical meter: segments arranged bottom to top
        CCoord segmentHeight = (totalHeight - (numSegments - 1) * segmentGap) / numSegments;
        CCoord y = viewSize.bottom - (i + 1) * segmentHeight - i * segmentGap;
        
        return CRect(viewSize.left, y, viewSize.right, y + segmentHeight);
    }
}

//------------------------------------------------------------------------
CColor LEDMeterView::getLitColor(int segmentIndex) const
{
    if (reduction)
        return amberLit;

    switch (Meter::segmentZone(segmentIndex, numSegments))
    {
        case Meter::kGreen:  return greenLit;
        case Meter::kYellow: return yellowLit;
        default:             return redLit;
    }
}

//------------------------------------------------------------------------
CColor LEDMeterView::getDarkColor(int segmentIndex) const
{
    if (reduction)
        return amberDark;

    switch (Meter::segmentZone(segmentIndex, numSegments))
    {
        case Meter::kGreen:  return greenDark;
        case Meter::kYellow: return yellowDark;
        default:             return redDark;
    }
}

//------------------------------------------------------------------------
} // namespace Yonie