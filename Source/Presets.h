#pragma once
#include <JuceHeader.h>
#include "Curve.h"

inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

struct FactoryPreset
{
    juce::String name;
    juce::String shapeId;
    std::vector<std::pair<juce::String, float>> values;  // param id -> real value
};

inline std::vector<FactoryPreset> getFactoryPresets()
{
    return {
        { "Init",                   "steps",   { {"rate",2}, {"depth",100}, {"click",2}, {"duck",0}, {"phaseOn",0}, {"spaceOn",0} } },
        { u8 ("Psytrance \xc2\xb7 Rolling"),    "steps",   { {"rate",2}, {"depth",100}, {"click",2}, {"duck",0}, {"phaseOn",0}, {"spaceOn",0} } },
        { u8 ("Psytrance \xc2\xb7 Full-On"),    "gate",    { {"rate",2}, {"depth",100}, {"click",1.5f}, {"duck",2}, {"bandLo",20}, {"bandHi",200}, {"phaseOn",1}, {"phaseMode",0}, {"split",90} } },
        { u8 ("Psytrance \xc2\xb7 Handover"),   "hold",    { {"rate",2}, {"depth",100}, {"click",2}, {"duck",2}, {"bandLo",20}, {"bandHi",200}, {"phaseOn",1}, {"phaseMode",0}, {"split",90}, {"spaceOn",1}, {"spaceAmount",30} } },
        { u8 ("Techno \xc2\xb7 Pump"),          "fast",    { {"rate",2}, {"depth",85}, {"click",3}, {"duck",0}, {"phaseOn",0}, {"spaceOn",0} } },
        { u8 ("Techno \xc2\xb7 Rumble"),        "log",     { {"rate",2}, {"depth",70}, {"click",3}, {"duck",1}, {"phaseOn",1}, {"phaseMode",0}, {"split",80} } },
        { u8 ("House \xc2\xb7 Breathe"),        "breathe", { {"rate",2}, {"depth",65}, {"click",4}, {"duck",0} } },
        { u8 ("House \xc2\xb7 Bounce"),         "bounce",  { {"rate",3}, {"depth",80}, {"click",3}, {"duck",0} } },
        { u8 ("Trance \xc2\xb7 Gate 1/16"),     "snap",    { {"rate",0}, {"depth",100}, {"click",1}, {"duck",0} } },
        { u8 ("DnB \xc2\xb7 Sub Only"),         "sigmoid", { {"rate",3}, {"depth",100}, {"click",2}, {"duck",1}, {"phaseOn",1}, {"split",100} } },
        { u8 ("Dubstep \xc2\xb7 Half-Time"),    "expo",    { {"rate",3}, {"depth",90}, {"click",2.5f}, {"duck",0} } },
        { "Keep The Top",           "steps",   { {"rate",2}, {"depth",100}, {"click",2}, {"duck",2}, {"bandLo",20}, {"bandHi",200} } },
        { u8 ("Gentle \xc2\xb7 Space Only"),    "breathe", { {"rate",2}, {"depth",20}, {"click",4}, {"duck",0}, {"spaceOn",1}, {"spaceAmount",60} } },
    };
}
