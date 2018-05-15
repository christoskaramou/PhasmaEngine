glslangValidator.exe -V General/shader.vert -o General/vert.spv
glslangValidator.exe -V General/shader.frag -o General/frag.spv
glslangValidator.exe -V GUI/shaderGUI.vert -o GUI/vert.spv
glslangValidator.exe -V GUI/shaderGUI.frag -o GUI/frag.spv
glslangValidator.exe -V Shadows/shaderShadows.vert -o Shadows/vert.spv
glslangValidator.exe -V SkyBox/shaderSkyBox.vert -o SkyBox/vert.spv
glslangValidator.exe -V SkyBox/shaderSkyBox.frag -o SkyBox/frag.spv
glslangValidator.exe -V Terrain/shaderTerrain.vert -o Terrain/vert.spv
glslangValidator.exe -V Terrain/shaderTerrain.frag -o Terrain/frag.spv
pause