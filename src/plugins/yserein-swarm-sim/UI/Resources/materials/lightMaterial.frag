VARYING vec4 vCustomData;
VARYING vec3 vGlobalPosition;

void MAIN()
{
    METALNESS = 0.0;
    ROUGHNESS = 1.0;
    FRESNEL_POWER = 5.0;

    BASE_COLOR = vec4(1.0,1.0,1.0,1.0);
    // EMISSIVE_COLOR = vec3(1.0, 1.0, 1.0);
}