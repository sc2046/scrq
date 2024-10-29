#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require
#include "host_device_common.h"
#include "device_common.glsl"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer { vec3 imageData[]; };
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Spheres { Sphere spheres[]; };

const uvec2 resolution = uvec2(800, 600);
const uint numSamples = 64;
const uint numBounces = 16;

vec3 rayColor(Ray ray, inout uint rngState);
void main()
{
	const uvec2 pixel = gl_GlobalInvocationID.xy;
	if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) { return; }

	// Use the linear index of the pixel as the initial seed for the RNG.
	uint rngState = resolution.x * pixel.y + pixel.x;

	// Camera parameters for Book 1 chapters 8-13.
	//Camera camera;
	//camera.center = vec3(0, 0, 0);
	//camera.eye = vec3(0, 0, -1);
	//camera.fovY = 90.f;
	//camera.focalDistance = 1.f;

	// Camera parameters for Book 1 chapter 14.
	Camera camera;
	camera.center			= vec3(13, 2, 3);
	camera.eye				= vec3(0, 0, 0);
	camera.fovY				= 20.f;
	camera.focalDistance	= 1.f;


	vec3 pixelColor = vec3(0.f);
	for (int sampleID = 0; sampleID < numSamples; ++sampleID)
	{
		Ray ray = generateRay(camera, vec2(pixel) + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState)), resolution);
		pixelColor += rayColor(ray, rngState);
	}
	pixelColor /= numSamples;
	const uint pixelStart = resolution.x * pixel.y + pixel.x;
	imageData[pixelStart] = pixelColor;
}

vec3 rayColor(Ray ray, inout uint rngState)
{
	vec3 curAttenuation = vec3(1.0);

	for (int depth = 0; depth < numBounces; ++depth)
	{
		// Initialise a ray query object.
		rayQueryEXT rayQuery;
		const float tMin = 0.f;
		const float tMax = 10000.f;
		rayQueryInitializeEXT(rayQuery, tlas, gl_RayFlagsOpaqueEXT, 0xFF, ray.origin, tMin, ray.direction, tMax);

		// Traverse scene, keeping track of the information at the closest intersection.
		HitInfo hitInfo;
		hitInfo.t = tMax;
		while (rayQueryProceedEXT(rayQuery))
		{
			// Because we are using procedural geometry (i.e. AABBs), we must check for intersections manually.
			if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionAABBEXT)
			{
				const int sphereID	= rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
				const Sphere sphere	= spheres[sphereID];

				if (hitSphere(sphere, ray, hitInfo)) {
					rayQueryGenerateIntersectionEXT(rayQuery, hitInfo.t);
				}
			}
		}
	
		if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			// No geometry intesections occured. Note that we treat the background as if it is a light source.
			const float a = 0.5 * (normalize(ray.direction).y + 1.0);
			const vec3 skyColor =  (1.0 - a) * vec3(1.0, 1.0, 1.0) + a * vec3(0.5, 0.7, 1.0);
			return curAttenuation * skyColor;
		}
		else if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			// Ignore triangles for now.
			return vec3(0.f);
		}
		else {
			vec3 attenuation;
			Ray scattered;

			const uint materialID = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rayQuery, true);
			bool scatter;
			switch (materialID) {
			case DIFFUSE:
				scatter = scatterDiffuse(ray, hitInfo, attenuation, scattered, rngState);
				break;
			case METAL:
				scatter = scatterMetal(ray, hitInfo, attenuation, scattered, rngState);
				break;
			case DIELECTRIC:
				scatter = scatterDielectric(ray, hitInfo, attenuation, scattered, rngState);
				break;
			default:
				scatter = false;
				break;
			}

			if (scatter) {
				curAttenuation *= attenuation;
				ray = scattered;
			}
			else return vec3(0.f);
		}
	}
	// Exceeded recursion - assume the sample provides no contribution to the light.
	return vec3(0.f);
}

