#include "Render/SceneSky.h"
#include "API/Image.h"
#include "Base/Path.h"
#include "Skybox/Skybox.h"

namespace pe
{
    void LoadDefaultSceneSky(CommandBuffer *cmd, SkyBox &day, SkyBox &night, Image *&iblBrdfLut)
    {
        day.LoadSkyBox(cmd, Path::Assets + "Skyboxes/golden_gate_hills/golden_gate_hills_4k.hdr");
        night.LoadSkyBox(cmd, Path::Assets + "Skyboxes/rogland_clear_night/rogland_clear_night_4k.hdr");

        Image::LoadRawParams loadImageParams = {256, 256, PE_FORMAT_R16G16_SFLOAT, false, true, 0.0f};
        iblBrdfLut = Image::LoadRaw(cmd, Path::Assets + "Objects/ibl_brdf_lut_rg16f_256.bin", loadImageParams);
    }

    void DestroyDefaultSceneSky(SkyBox &day, SkyBox &night, Image *&iblBrdfLut)
    {
        day.Destroy();
        night.Destroy();
        Image::Destroy(iblBrdfLut);
    }
} // namespace pe
