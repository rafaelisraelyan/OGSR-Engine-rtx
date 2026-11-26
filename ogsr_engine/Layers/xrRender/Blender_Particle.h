// Blender_Screen_SET.h: interface for the Blender_Screen_SET class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

class CBlender_Particle : public IBlenderXr
{
    xrP_TOKEN oBlend;

    xrP_BOOL oClamp;

public:
    virtual LPCSTR getComment() { return "particles"; }

    virtual void Save(IWriter& fs);
    virtual void Load(IReader& fs, u16 version);
    
    virtual void SaveIni(CInifile* ini_file, LPCSTR section);
    virtual void LoadIni(CInifile* ini_file, LPCSTR section);

    virtual void Compile(CBlender_Compile& C);

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
    bool ReadNativeParticleSource(u32& blendId, u32& priority, bool& clampTexture, bool& strictSorting) const
    {
        if (oBlend.IDselected >= 6 || oPriority.value < 0 || oPriority.value > 3)
            return false;
        blendId = oBlend.IDselected;
        priority = static_cast<u32>(oPriority.value);
        clampTexture = oClamp.value != FALSE;
        strictSorting = oStrictSorting.value != FALSE;
        return true;
    }
#endif

    CBlender_Particle();
    virtual ~CBlender_Particle();
};
