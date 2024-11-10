#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable

precision highp float;
#define PI 3.1415926535897932384626433832
#define ex 2.7182818284590452353602874713

struct LightBufferData
{
    vec3 position;
    vec3 light_color;
    vec3 sun_pos;
    float strength;
    float time_of_the_day;
};

layout(set = 0, binding = 0) uniform UniformWorld{
    mat4 view;
    mat4 proj;
    vec3 pos;
    LightBufferData light;
} world;

layout(set = 1, binding = 0) uniform samplerCube skybox_sampler;

layout(set = 3, binding = 0) uniform Atmosphere
{
    float sun_distance;
    float mie_coefficient;
    float mie_scattering_dir;
    float rayleigh_scatter;

    float turbidity;
    float luminance;
    float sun_exposure;
} atmosphere;

layout(location = 0) in vec3 frag_dir;
layout(location = 0) out vec4 out_color;

/**
 * CONSTANTS
 */
const vec3 up_dir = vec3( 0.0, 1.0, 0.0 );

// Rayleigh coefficients
// This function is redundant, as the scatter coefficient has been pre-computed in the constant below.
// Can still be interesting to see though...
/* // Wavelength of used primaries, according to Preetham:
// (http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2012/10/ATI-LightScattering.pdf)
const vec3 lambda = vec3( 680E-9, 550E-9, 450E-9 );
const float nAir = 1.0003; // Refractive index of Air
const float NAir = 2.545E25; // Number of molecules per unit volume for Air
              // (at 288.15K and 1013mb (15 degrees celsius at sea level)) 
vec3 RayLeighScatterCoefficient(vec3 lambda) {
  return (8.0 * pow(PI, 3.0) * pow(pow(nAir, 2.0) - 1.0, 2.0) * (6.0 + 3.0 * pn)) / (3.0 * NAir * pow(lambda, vec3(4.0)) * (6.0 - 7.0 * pn));
}*/
const vec3 RayleighConst = vec3( 5.804542996261093E-6, 1.3562911419845635E-5, 3.0265902468824876E-5 );

// Mie coefficients
// Same thing here, save the computations
/* // K coefficient for the primaries
const float v = 4.0;
const vec3 K = vec3( 0.686, 0.678, 0.666 );
MieConst = PI * pow( ( 2.0 * PI ) / lambda, vec3( v - 2.0 ) ) * K; */
const vec3 MieConst = vec3( 1.8399918514433978E14, 2.7798023919660528E14, 4.0790479543861094E14 );

// Optical depth at zenith for molecules
const float rayleighZenithLength = 8.4E3;
const float mieZenithLength = 1.25E3;

// Used to calculate the solar disc:
// Equals: cos(sunSize*pi/180.0)
const float sunAngularDiameterCos = 0.999956676946448443553574619906976478926;

/**
 * HELPER FUNCTIONS
 */

// Calculate Phase for Rayleigh (simplified equation, as per Preetham)
float rayleighPhase( float cosTheta ) {
	return (3.0 / (16.0*PI)) * ( 1.0 + pow( cosTheta, 2.0 ) );
}

// Calculate Phase for Mie (full equation, as per Preetham)
float aerosolPhase( float cosTheta, float g ) {
	float gg = g*g;
	float denominator = 1.0 / ( (4.0*PI) * pow(1.0 + gg - 2.0*g*cosTheta, 1.5) );
	return pow(1.0 - g, 2.0) * denominator;
}

// Earth horizon cuttoff hack (Stolen)
const float cutoffAngle = 1.6110731556870734; // = PI / 1.95;
const float steepness = 1.5;

// Calculate the intensity of the sun and add a horizon cutoff!
float sunIntensity( float zenithAngleCos ) {
  zenithAngleCos = clamp( zenithAngleCos, -1.0, 1.0 ); // Just to be cautious.
  return atmosphere.sun_exposure* max( 0.0, 1.0 - pow( ex, -( ( cutoffAngle - acos( zenithAngleCos ) ) / steepness ) ) );
}

// Calculate the total Mie scattering coefficient depending of the air's turbidity.
vec3 totalMie( float Turb ) {
  float c = ( 0.2 * Turb ) * 10E-18;
  return 0.434 * c * MieConst;
}

// Function to get a tonemapping with a filmic feel, as per Uncharted 2:
// http://filmicworlds.com/blog/filmic-tonemapping-operators/ 
const float A = 0.15;
const float B = 0.50;
const float C = 0.10;
const float D = 0.20;
const float E = 0.02;
const float F = 0.30;

vec3 tonemap( vec3 x ) {
	return ( (x * (A*x + C*B) + D*E) / (x * (A*x + B) + D*F) ) - E/F;
}


void main()
{
    vec3 view_dir = normalize(frag_dir);
    vec3 sun_dir = normalize(world.light.sun_pos);

    float sun_int = sunIntensity(dot(sun_dir, up_dir));
    float sun_fade = 1.0 - clamp( 1.0 - exp(world.light.sun_pos.y / atmosphere.sun_distance), 0.0, 1.0);

    float rayleigh_coefficient = atmosphere.rayleigh_scatter - (0.0 * (1.0 - sun_fade));

    vec3 total_rayleigh_coeff = RayleighConst * rayleigh_coefficient;
    vec3 total_mie_coeff = totalMie(atmosphere.turbidity) * atmosphere.mie_coefficient;


    float zenith_angle = acos(max(0.0, dot(up_dir, view_dir)));
    float avg_density_height = 0.25;

    float denom = 1.0 / (cos(zenith_angle) + avg_density_height * pow(93.885 - ((zenith_angle * 180.0) / PI), -1.253));
    float ray_optical_depth = rayleighZenithLength * denom;
    float mie_optical_depth = mieZenithLength * denom;

    vec3 tot_extinction = exp( -(total_rayleigh_coeff*ray_optical_depth + total_mie_coeff*mie_optical_depth));

    float cos_theta = dot(view_dir, sun_dir);
    float ray_phase = rayleighPhase(cos_theta * 0.5 + 0.5);
    vec3 beta_ray_theta = total_rayleigh_coeff * ray_phase;

    float mie_phase = aerosolPhase(cos_theta, atmosphere.mie_scattering_dir);
    vec3 beta_mie_theta = total_mie_coeff * mie_phase;



    vec3 accumulated_scattering = (beta_ray_theta + beta_mie_theta) / (total_rayleigh_coeff + total_mie_coeff);
    vec3 in_light = pow(sun_int * accumulated_scattering * (1.0 - tot_extinction), vec3(1.5));
    in_light *= mix(vec3(1.0), pow(sun_int * accumulated_scattering * tot_extinction, vec3(0.5)),
                    clamp(pow(1.0 - dot(up_dir, sun_dir), 5.0), 0.0, 1.0));

    vec3 light_solar_disc = vec3(0.1) * tot_extinction;
    float sun_disc = smoothstep(sunAngularDiameterCos, sunAngularDiameterCos + 0.00002, cos_theta);
    light_solar_disc += (sun_int * 19000.0 * tot_extinction) * sun_disc;

    vec3 tex_color = (in_light + light_solar_disc) * 0.04 + vec3(0.0, 0.0003, 0.000075);

    float exposure_bias = log2(2.0/pow(atmosphere.luminance, 4.0));
    //vec3 curr = tonemap(exposure_bias * tex_color);
    //vec3 white_scale = 1.0 / tonemap(vec3(1000.0));
    //vec3 color = curr * white_scale;

    // Tonemapping. Maybe skip as we do it in post processing.
    vec3 curr = exposure_bias * tex_color;
    //vec3 white_scale = 1.0 / tonemap(vec3(1000.0));
    vec3 color = curr;

    //vec3 retColor = pow( color, vec3( 1.0 / (1.2 + (1.2 * sun_fade) ) ) );
    
    //out_color = vec4(retColor, 1.0);

    out_color = vec4(color, 1.0f);
    //out_color = texture(skybox_sampler, normalize(frag_dir));
}