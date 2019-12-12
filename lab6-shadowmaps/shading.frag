#version 420

// required by GLSL spec Sect 4.5.3 (though nvidia does not, amd does)
precision highp float;

///////////////////////////////////////////////////////////////////////////////
// Material
///////////////////////////////////////////////////////////////////////////////
uniform vec3 material_color;
uniform float material_reflectivity;
uniform float material_metalness;
uniform float material_fresnel;
uniform float material_shininess;
uniform float material_emission;

uniform int has_color_texture;
layout(binding = 0) uniform sampler2D colorMap;
uniform int has_emission_texture;
layout(binding = 5) uniform sampler2D emissiveMap;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
layout(binding = 6) uniform sampler2D environmentMap;
layout(binding = 7) uniform sampler2D irradianceMap;
layout(binding = 8) uniform sampler2D reflectionMap;
uniform float environment_multiplier;

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
uniform vec3 point_light_color = vec3(1.0, 1.0, 1.0);
uniform float point_light_intensity_multiplier = 50.0;

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////
#define PI 3.14159265359

///////////////////////////////////////////////////////////////////////////////
// Input varyings from vertex shader
///////////////////////////////////////////////////////////////////////////////
in vec2 texCoord;
in vec3 viewSpaceNormal;
in vec3 viewSpacePosition;

///////////////////////////////////////////////////////////////////////////////
// Input uniform variables
///////////////////////////////////////////////////////////////////////////////
uniform mat4 viewInverse;
uniform vec3 viewSpaceLightPosition;

///////////////////////////////////////////////////////////////////////////////
// Output color
///////////////////////////////////////////////////////////////////////////////
layout(location = 0) out vec4 fragmentColor;

///////////////////////////////////////////////////////////////////////////////
// Shaders
///////////////////////////////////////////////////////////////////////////////
//uniform mat4 lightMatrix; Changed
in vec4 shadowMapCoord;
//layout(binding = 10) uniform sampler2D shadowMapTex;
layout(binding = 10) uniform sampler2DShadow shadowMapTex;


uniform vec3 viewSpaceLightDir;
uniform float spotOuterAngle;
uniform float spotInnerAngle;

vec3 calculateDirectIllumiunation(vec3 wo, vec3 n, vec3 base_color)
{
	vec3 direct_illum = base_color;

	float d = length(viewSpaceLightPosition - viewSpacePosition);
	vec3 Li = point_light_intensity_multiplier * point_light_color * 1/(d*d);
	vec3 wi = normalize(viewSpaceLightPosition - viewSpacePosition);

	if(dot(n,wo)<= 0){
		return vec3(0);
	}

	vec3 diffuse_term = material_color * 1/PI * dot(n,wi) * Li;

	vec3 wh = normalize(wi + wo);
	float s = material_shininess;
	float D = ((s+2)/(2*PI))*pow(dot(n,wh),s);
	float F = material_fresnel + (1 - material_fresnel)*pow((1-dot(wi,wh)),5); 
	float G = min(1, min(2 * (dot(n, wh)*dot(n, wo))/dot(wo,wh), 2 * (dot(n, wh)*dot(n, wi))/dot(wo, wh)));
	float brdf = F*D*G/(4*dot(n,wo)*dot(n,wi));

	vec3 dielectric_term = brdf * dot(n,wi)*Li + (1 - F) * diffuse_term;
	vec3 metal_term = brdf * material_color * dot(n,wi)*Li;

	vec3 microfacet_term = material_metalness * metal_term + (1-material_metalness) * dielectric_term;

	return material_reflectivity * microfacet_term + (1-material_reflectivity) * diffuse_term;

}

vec3 calculateIndirectIllumination(vec3 wo, vec3 n)
{
	vec3 indirect_illum = vec3(0.f);

	vec3 nws = normalize(vec3(viewInverse * vec4(n,0))); //Normal world space

	// Calculate the spherical coordinates of the direction
	float theta = acos(max(-1.0f, nws.y));
	float phi = atan(nws.z, nws.x);
	if(phi < 0.0f){
		phi = phi + 2.0f * PI;
	}

	// Use these to lookup the color in the environment map
	vec2 lookup = vec2(phi / (2.0 * PI), theta / PI);

	// Look-up
	vec4 irradiance = environment_multiplier * texture(irradianceMap, lookup);
	vec4 diffuse_term = vec4(material_color,0) * (1.0 / PI) * irradiance;

	vec3 wi = normalize(viewSpaceLightPosition - viewSpacePosition);
	vec3 wh = normalize(wo + wi);

	vec3 wows = normalize(vec3(viewInverse * vec4(wo, 0))); //Wo world space
	vec3 wiws = reflect(-wows, nws);

	theta = acos(max(-1.0f, min(1.0f, wiws.y)));
	phi = atan(wiws.z, wiws.x);
	if(phi < 0.0f){
		phi = phi + 2.0f * PI;
	}
	lookup = vec2(phi / (2.0 * PI), theta / PI);


	float roughness = sqrt(sqrt(2/(material_shininess+2)));
	vec3 Li = environment_multiplier * textureLod(reflectionMap, lookup, roughness * 7.0).xyz;
	float F = material_fresnel + (1 - material_fresnel) * pow(1-dot(wh,wi),5);

	vec3 dielectric_term = F*Li + (1-F) * vec3(diffuse_term);
	vec3 metal_term = F * material_color * Li;

	vec3 microfacet_term = material_metalness * metal_term + (1 - material_metalness) * dielectric_term;
	return material_reflectivity * microfacet_term + (1-material_reflectivity) * vec3(diffuse_term);

}

void main()
{
	//float visibility = 1.0;
	float attenuation = 1.0;

	vec3 base_color = material_color;
	if(has_color_texture == 1)
	{
		base_color *= texture(colorMap, texCoord).xyz;
	}

	vec3 wo = -normalize(viewSpacePosition);
	vec3 n = normalize(viewSpaceNormal);

	//float depth = texture(shadowMapTex, shadowMapCoord.xy / shadowMapCoord.w).x;
	//float visibility = (depth >= (shadowMapCoord.z / shadowMapCoord.w)) ? 1.0 : 0.0;
	float visibility = textureProj( shadowMapTex, shadowMapCoord );

	vec3 posToLight = normalize(viewSpaceLightPosition - viewSpacePosition);
	float cosAngle = dot(posToLight, -viewSpaceLightDir);

	// Spotlight with hard border:
	//float spotAttenuation = (cosAngle > spotOuterAngle) ? 1.0 : 0.0;
	float spotAttenuation = smoothstep(spotOuterAngle, spotInnerAngle, cosAngle);
	visibility *= spotAttenuation;

	// Direct illumination
	vec3 direct_illumination_term = visibility * calculateDirectIllumiunation(wo, n, base_color);

	// Indirect illumination
	vec3 indirect_illumination_term = calculateIndirectIllumination(wo, n);

	///////////////////////////////////////////////////////////////////////////
	// Add emissive term. If emissive texture exists, sample this term.
	///////////////////////////////////////////////////////////////////////////
	vec3 emission_term = material_emission * material_color;
	if(has_emission_texture == 1)
	{
		emission_term = texture(emissiveMap, texCoord).xyz;
	}

	vec3 shading = direct_illumination_term + indirect_illumination_term + emission_term;

	fragmentColor = vec4(shading, 1.0);
	return;
}
