//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "depthswitch.h"

#include "vstgui/lib/cbitmap.h"

#include <string>
#include <vector>

using namespace VSTGUI;

namespace Yonie {

namespace {

// strtod and atof follow LC_NUMERIC, and a host that runs in the user's locale (Ardour)
// with a decimal comma read "25.5" as 25 - every click missed. The geometry strings are
// always written with a dot, so parse them without the C library: sign, digits, an
// optional fraction and an optional exponent.
const char* parseNumber(const char* p, double& v)
{
    const char* s = p;
    double sign = 1.0;
    if (*s == '-' || *s == '+')
        sign = (*s++ == '-') ? -1.0 : 1.0;
    if (!((*s >= '0' && *s <= '9') || (*s == '.' && s[1] >= '0' && s[1] <= '9')))
        return p;
    double x = 0.0;
    while (*s >= '0' && *s <= '9')
        x = x * 10.0 + (*s++ - '0');
    if (*s == '.')
    {
        double scale = 0.1;
        for (++s; *s >= '0' && *s <= '9'; ++s, scale *= 0.1)
            x += (*s - '0') * scale;
    }
    if ((*s == 'e' || *s == 'E') && ((s[1] >= '0' && s[1] <= '9') ||
        ((s[1] == '-' || s[1] == '+') && s[2] >= '0' && s[2] <= '9')))
    {
        ++s;
        const int esign = (*s == '-') ? -1 : 1;
        if (*s == '-' || *s == '+')
            ++s;
        int e = 0;
        while (*s >= '0' && *s <= '9')
            e = e * 10 + (*s++ - '0');
        for (int k = 0; k < e; ++k)
            x = esign > 0 ? x * 10.0 : x / 10.0;
    }
    v = sign * x;
    return s;
}

// "a,b,c,d" -> the numbers. Same shape of parser as ModeSwitch's, so the two
// views take their geometry in the same form from the same script.
std::vector<double> parseNumbers(const std::string& spec)
{
    std::vector<double> out;
    size_t i = 0;
    while (i < spec.size())
    {
        size_t j = spec.find(',', i);
        if (j == std::string::npos)
            j = spec.size();
        double v = 0.0;
        const std::string field = spec.substr(i, j - i);
        const char* f = field.c_str();
        while (*f == ' ')
            ++f;
        parseNumber(f, v);
        out.push_back(v);
        i = j + 1;
    }
    return out;
}

} // namespace

//------------------------------------------------------------------------
DepthSwitch::DepthSwitch(const CRect& size)
: CControl(size, nullptr, -1)
{
    setWantsFocus(false);
}

//------------------------------------------------------------------------
void DepthSwitch::setButtonRect(const std::string& spec)
{
    buttonRectSpec = spec;
    const auto n = parseNumbers(spec);
    if (n.size() >= 4)
        button = CRect(n[0], n[1], n[2], n[3]);
}

//------------------------------------------------------------------------
void DepthSwitch::setFrameRect(const std::string& spec)
{
    frameRectSpec = spec;
    const auto n = parseNumbers(spec);
    if (n.size() >= 4)
        frame = CRect(n[0], n[1], n[2], n[3]);
}

//------------------------------------------------------------------------
void DepthSwitch::setLED(const std::string& spec)
{
    ledPosSpec = spec;
    const auto n = parseNumbers(spec);
    if (n.size() >= 4)
        led = CRect(n[0], n[1], n[2], n[3]);
}

//------------------------------------------------------------------------
void DepthSwitch::setGlow(const std::string& spec)
{
    glowPosSpec = spec;
    const auto n = parseNumbers(spec);
    if (n.size() >= 4)
        glow = CRect(n[0], n[1], n[2], n[3]);
}

//------------------------------------------------------------------------
void DepthSwitch::draw(CDrawContext* context)
{
    const CPoint origin(getViewSize().left, getViewSize().top);
    const bool deep = isDeep();

    // The button, from the two-frame filmstrip: frame 0 is out, frame 1 is
    // pressed in. Both frames are rendered by the toolkit against the same
    // lamp as the knobs, so the part belongs to the panel rather than sitting
    // on top of it.
    if (CBitmap* strip = getBackground())
    {
        // The filmstrip's own rect, not the view: the view is larger still,
        // because the lamp sits above the button. Drawing the strip across the
        // whole view would stretch it.
        CRect r = frame;
        r.offset(origin.x, origin.y);
        const CCoord frameH = strip->getHeight() / 2;
        const CPoint off(0, deep ? frameH : 0);
        strip->draw(context, r, off);
    }

    // The lamp, above the button.
    //
    // The lens is a rendered part, two frames - dark and lit - so it is a
    // domed piece of tinted plastic in a holder lit by the same lamp as
    // everything else, rather than a stack of ellipses drawn here. Only its
    // SPILL is drawn at runtime, because that falls on the panel and the panel
    // is not part of the render.
    if (!led.isEmpty())
    {
        CRect r = led;
        r.offset(origin.x, origin.y);

        // The spill goes down FIRST, so the lamp sits in it rather than
        // behind it.
        if (deep)
            if (CBitmap* spill = glowSprite)
            {
                CRect g = glow;
                g.offset(origin.x, origin.y);
                spill->draw(context, g);
            }

        if (CBitmap* lamp = ledStrip)
        {
            const CCoord frameH = lamp->getHeight() / 2;
            lamp->draw(context, r, CPoint(0, deep ? frameH : 0));
        }
    }

    setDirty(false);
}

//------------------------------------------------------------------------
CMouseEventResult DepthSwitch::onMouseDown(CPoint& where, const CButtonState& buttonState)
{
    if (!buttonState.isLeftButton())
        return kMouseEventNotHandled;

    CPoint local(where);
    local.offset(-getViewSize().left, -getViewSize().top);

    // Only the button itself, not the panel around it. Clicking dead panel
    // should do nothing at all - the bug WetReverb shipped for two releases was
    // exactly this kind of slack hit area.
    if (!button.pointInside(local))
        return kMouseEventNotHandled;

    beginEdit();
    setValueNormalized(isDeep() ? 0.f : 1.f);
    valueChanged();
    endEdit();
    invalid();
    return kMouseEventHandled;
}

//------------------------------------------------------------------------
} // namespace Yonie
